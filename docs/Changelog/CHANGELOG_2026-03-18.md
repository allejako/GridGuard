# Testsvit utökad: 74 → 146 tester + Byggfixar

**Branch:** `development`
**Datum:** 2026-03-18
**Commit:** `4f1fc18`
**Status:** Pushad

---

## Bakgrund

Sedan föregående test-commit (`032b80d`) hade byggsystemet ett par felkonfigurerade länkberoenden som orsakade kompileringsfel när `RuntimeConfig` lades till som en systemkärnkomponent. Dessutom saknades testsvit för flera centrala moduler: JWT-autentisering, runtime-konfiguration, API-URL-byggaren, compute-motorn, databaskretsen och synkroniseringsprimitiver.

Denna commit åtgärdar båda problemen.

---

## Byggfixar (Makefile)

### Duplicerat `SignalHandler.c` i `SERVER_SRCS`

```makefile
# Före — SignalHandler.c listades två gånger
$(SRC)/sys/SignalHandler.c $(SRC)/sys/Daemon.c $(SRC)/sys/PidFile.c $(SRC)/sys/SignalHandler.c \

# Efter — deduplicerat
$(SRC)/sys/Daemon.c $(SRC)/sys/PidFile.c $(SRC)/sys/SignalHandler.c \
```

Dubbel källfil gick igenom länkaren utan fel men genererade onödiga varningar och potentiella ODR-konflikter.

### Saknade länkberoenden för integrationstester

`test_api`, `test_weather` och `test_pipeline` refererade alla till symboler i `RuntimeConfig.c` och `ConfigParser.c` men länkades inte mot dem — resulterade i `undefined reference`-fel vid build.

```makefile
# Tillagt i test_api, test_weather och test_pipeline:
$(SRC)/config/RuntimeConfig.c $(SRC)/config/ConfigParser.c
```

### `ScheduleDB.c` borttaget från `test_pipeline`

`ScheduleDB.c` länkades in i `test_pipeline` men refererades aldrig — borttaget för att hålla beroendeträdet rent.

### `test-pipeline` borttaget från standard-`test`-målet

`test-pipeline` är ett integrationstest som kräver att hela systemet (watchdog + fetcher + parser) körs. `GridGuard_Initiate` försöker öppna `/tmp/gridguard_requests.fifo` för skrivning — en FIFO som watchdog skapar och som fetcher-processen lyssnar på. Utan en levande fetcher på andra sidan blockerar eller failar `open(O_WRONLY)` direkt.

Testet kan inte köras fristående och är därför uteslutet från `make test`. Det kan fortfarande köras manuellt mot ett levande system:

```bash
make dev          # Starta systemet
make test-pipeline  # Kör i separat terminal
```

---

## Nya testfiler (8 st)

Alla nya tester är skrivna med Google Test och registrerade i `tests/CMakeLists.txt`.

### 1. `test_jwt_gtest.cpp` — JWT-autentisering

Testar `JWTIssuer` och `JWTValidator` gemensamt: att utfärdade tokens valideras korrekt, att manipulerade tokens avvisas, och att utgångna tokens hanteras rätt.

**Vad som täcks:**
- Token-utfärdning och signaturvalidering (mbedTLS)
- Manipulation av header/payload/signatur → förväntat avvisande
- Utgångna tokens (`exp`-claim)
- Felaktiga secrets → validering misslyckas

**Varför detta är viktigt:**
GridGuard exponerar ett REST-API med JWT-skyddade endpoints. Om `JWTValidator` accepterar ogiltiga tokens kan vem som helst skicka kommandon till systemet.

---

### 2. `test_runtime_config_gtest.cpp` — Tre-nivå-konfigurationsfallback

Testar `RuntimeConfig`-modulens prioritetsordning: INI-fil → miljövariabel → hårdkodat default.

**Vad som täcks:**
- Värde från INI-fil läses korrekt
- Miljövariabel åsidosätter INI-fil
- Default används när varken fil eller env finns
- Ogiltig INI-syntax hanteras utan krasch

**Varför detta är viktigt:**
RuntimeConfig är det centrala konfigurationslagret som alla moduler förlitar sig på. Felaktig prioritetsordning leder till att produktionssystemet körs med felaktiga värden.

---

### 3. `test_api_endpoints_gtest.cpp` — URL-byggaren

Testar att `APIEndpoints` konstruerar korrekta URL:er för externa API-anrop (spotpriser, väder).

**Vad som täcks:**
- Korrekt URL-format för Elpriset API
- Korrekt URL-format för Open-Meteo API
- Att datumparametrar interpoleras rätt
- Gränsfall: månadsskiften, årsbyten

**Varför detta är viktigt:**
Felaktiga URL:er ger 404 eller felaktig data — systemet gör då energibeslut baserat på garbage-data.

---

### 4. `test_compute_gtest.cpp` — Compute-motorn

Testar att `Compute`-modulen genererar rätt styrsignal (BUY / SELL / AVOID / IDLE) baserat på inkommande energiplan.

**Vad som täcks:**
- BUY-signal när priset är tillräckligt lågt
- SELL-signal när solelproduktionen överstiger förbrukning och priset är högt
- AVOID-signal vid extremt höga priser
- IDLE när inga villkor är uppfyllda
- Tröskelvärden för alla signalovergångar

**Varför detta är viktigt:**
Det här är GridGuards kärnlogik. En felaktig styrsignal kan kosta pengar (köper dyrt, säljer billigt) eller i värsta fall skada utrustning.

---

### 5. `test_work_completion_gtest.cpp` — Synkroniseringsprimitiv

Stress-testar `WorkCompletion` — en one-shot signal/wait-primitiv som används för att koordinera asynkront arbete mellan trådar.

**Vad som täcks:**
- Signal innan wait → wait returnerar omedelbart
- Wait innan signal → wait blockerar tills signal
- Multipla waiters på samma completion
- Timeout-hantering
- Reset och återanvändning

---

### 6. `test_schedule_db_gtest.cpp` — ScheduleDB

Testar CRUD-operationer mot `ScheduleDB` (SQLite-backed lagring av schemalagda fönster).

**Vad som täcks:**
- Insert och läsning av ScheduleWindow
- Uppdatering av status (PENDING → ACTIVE → DONE)
- Borttagning av utgångna poster
- Konfliktdetektering vid överlappande fönster

---

### 7. `test_user_config_db_gtest.cpp` — UserConfigDB

Testar CRUD-operationer mot `UserConfigDB` (per-enhet konfiguration lagrad i SQLite).

**Vad som täcks:**
- Insert och läsning av användarkonfiguration
- Uppdatering av enskilda fält
- Standardvärden när post saknas
- Felhantering vid korrupt data

---

### 8. `test_heartbeat_gtest.cpp` — Heartbeat-övervakning

Testar att heartbeat-modulen korrekt detekterar när en övervakad process slutar skicka livstecken.

**Vad som täcks:**
- Heartbeat inom timeout → process anses levande
- Utebliven heartbeat → process flaggas som dead
- Återupptagning av heartbeat → status återställs
- Konfigurerbar timeout-tröskel

---

## Uppdaterad `test_scheduler_gtest.cpp`

De befintliga scheduler-testerna innehöll ett grundläggande fel: testdata antog att varje slot var **1 timme**, men schedulern arbetar med **15-minuters slots (900 s)**. Testerna passerade trots felet eftersom förväntade värden också var fel — ett klassiskt "wrong for the right reason"-scenario.

**Korrigering:**
```cpp
// Före — felaktig granularitet
entry.timestamp = baseTime + (i * 3600); // 1 timme per slot

// Efter — korrekt granularitet
entry.timestamp = baseTime + (i * 900);  // 15 minuter per slot
```

Alla förväntade kostnadsvärden och start-timestamps i testerna uppdaterades i enlighet med detta.

---

## `tests/CMakeLists.txt` — Nya build-targets

Samtliga nya test-executables är registrerade med `gtest_discover_tests`. Nödvändiga bibliotek länkas per target:

| Target | Extra libs |
|--------|------------|
| `test_jwt_gtest` | mbedtls, mbedx509, mbedcrypto |
| `test_schedule_db_gtest` | sqlite3 |
| `test_user_config_db_gtest` | sqlite3 |
| Övriga | — |

`find_library` används för mbedTLS och SQLite3 för att fungera på olika installationssökvägar.

---

## Testresultat

```
Före: 74 tester
Efter: 146 tester
Delta: +72 tester (+97%)
```

Alla 146 tester passerar med ASAN/UBSAN aktiverat.

---

## Filändringar

**Modifierade filer:**
- `Makefile` — fixat duplicerat `SignalHandler.c`, lagt till saknade `RuntimeConfig`/`ConfigParser`-beroenden, borttaget `test-pipeline` från standard-`test`-målet
- `tests/CMakeLists.txt` — lagt till 8 nya test-executables med korrekt länkning
- `tests/unit/test_scheduler_gtest.cpp` — korrigerat slot-granularitet från 1 h till 15 min

**Nya filer:**
- `tests/unit/test_jwt_gtest.cpp` (132 rader)
- `tests/unit/test_runtime_config_gtest.cpp` (124 rader)
- `tests/unit/test_api_endpoints_gtest.cpp` (125 rader)
- `tests/unit/test_compute_gtest.cpp` (187 rader)
- `tests/unit/test_work_completion_gtest.cpp` (100 rader)
- `tests/unit/test_schedule_db_gtest.cpp` (138 rader)
- `tests/unit/test_user_config_db_gtest.cpp` (128 rader)
- `tests/unit/test_heartbeat_gtest.cpp` (97 rader)

**Total ny testkod:** ~1 031 rader C++.

---

## Hur man kör

```bash
# Alla tester via CMake
make test-gtest

# Specifik svit
./build/tests/test_jwt_gtest
./build/tests/test_compute_gtest
./build/tests/test_scheduler_gtest

# Med filter
./build/tests/test_compute_gtest --gtest_filter="*BUY*"
```
