# GridGuard — Projektläge och vad som återstår

**Datum:** 2026-03-02
**Kurs:** Systemprogrammering och introduktion till C++ (vecka 1–12)
**Examination:** Kursvecka 12 — skriftligt kunskapstest (kursmål 1–6), projektinlämning (kursmål 7–12)

---

## Vad vi har gjort

### Kursvecka 1–2: Processer och trådar

Systemet kör tre separata processer som startas via `fork()` + `execl()`. Servern (`GridGuard-server`) spawnear `GridGuard-fetcher` och `GridGuard-parser` vid uppstart. Fetcherprocessen tar emot sin anonyma pipe-ände via `dup2()` innan `execl()`.

HTTP-requestsen hanteras av en `ThreadPool` med 20 worker-trådar. Varje tråd tar jobb från en delad `Queue` med `pthread_cond_wait`. Compute-tråden körs separat i main-processen och kommunicerar med parser-processen via Unix socket.

Watchdog-processen spawnar daemon-servern med `fork()` + `execl()`, övervakar den via en heartbeat-pipe och startar om vid krasch med exponentiellt backoff.

Binärernas sökvägar löses vid runtime via `readlink("/proc/self/exe")` + `dirname()` så att koden fungerar på vilken maskin som helst, inte bara den den kompilerades på.

**Täcker kursmål 1 och 7.**

---

### Kursvecka 3: Mutex och villkorsvariabler

`WorkCompletion` är en one-shot completion-primitiv — HTTP-tråden blockerar på `pthread_cond_timedwait` (30 sekunders timeout) och compute-tråden väcker den med `pthread_cond_broadcast` när energiplanen är klar.

`CompletionRegistry` är en global array som mappar userId till rätt `WorkCompletion`. Alla operationer skyddas av en `pthread_mutex_t`. Registret tömmer posten för ett userId *innan* signalen skickas, så att nästa request för samma användare börjar med en ren slot.

`Queue` implementerar producer-consumer-mönstret med `notEmpty` och `notFull` condition variables — exakt det mönster som kursvecka 3 tar upp.

**Täcker kursmål 1 och 7.**

---

### Kursvecka 4: Anonyma och namngivna pipes

HTTP-tråden skriver `WorkRequest`-structar till write-änden av en anonym pipe. Fetcher-processen läser från sin stdin, som är dup2:ad till read-änden av samma pipe.

Fetcher skriver `FetchResult` (väder-JSON + pris-JSON) till en named FIFO (`/tmp/gridguard_fetch_to_parse.fifo`). Parser-processen läser från den. FIFO:n skapas med `mkfifo()` och tas bort vid shutdown.

**Täcker kursmål 2 och 8.**

---

### Kursvecka 5: Unix sockets, delat minne och semaforer

Parser-processen lyssnar på en Unix domain socket (`/tmp/gridguard_parse_to_compute.sock`). Compute-tråden ansluter som klient och läser en `ParseResult`-struct per anslutning, ett request per connection.

`SharedCache` implementerar en cross-process cache via `shm_open()` + `ftruncate()` + `mmap()`. Servern skapar segmenten (`/gridguard_weather`, `/gridguard_price`), fetcher-processen attacher till dem. Named POSIX-semaforer (`sem_open`) skyddar cache-skrivningar mot race conditions.

TTL är 15 minuter. Om en request för samma koordinater och datum kommer inom 15 minuter gör fetcher inget HTTP-anrop — parser och compute får cachad data direkt.

**Täcker kursmål 2 och 8.**

---

### Beräkningslogik: BUY/SELL/IDLE

Compute-modulen beräknar verklig konsumentkostnad per timme:

```
totalkostnad = (spotpris + nätavgift + energiskatt 0.40) × 1.25 moms
```

Nätavgiften väljs beroende på timme (låg 00–06, normal 07–16, hög 17–23) och läses från användarens konfiguration i databasen.

BUY-signalen ges till de billigaste 30% av timmarna i prognonfönstret (30:e percentilen av totalkostnad). Det ger alltid ett förutsägbart antal BUY-timmar oavsett om priserna den dagen är stabila eller volatila.

SELL ges bara när solöverskottet är positivt *och* spotpriset är ≥ 0. Vid negativa spotpriser (exportören betalar) väljer systemet IDLE och låter huset konsumera sin egenkraft.

Varje timme exponerar `savings_vs_median_sek_kwh` — hur mycket billigare timmen är jämfört med mediantimmen. En kund som ser BUY kl 03:00 med `savings_vs_median_sek_kwh: 0.42` och behöver ladda 10 kWh vet att det sparar ~4.20 kr jämfört med att vänta.

---

### Solproduktionsmodell: temperatur- och vindkorrigering

Open-Meteo levererar tre väderfält per timme som nu används i produktionsberäkningen: `solarIrradiance` (W/m²), `temperature` (°C) och `windSpeed` (m/s). Tidigare användes bara strålningen.

Modellen följer IEC 61215 NOCT-standarden (Nominal Operating Cell Temperature):

```
T_cell = T_ambient + (45 - 20) / (800 × (1 + 0.04 × vindHastighet)) × strålning_W/m²
tempFactor = 1 + (-0.0045) × (T_cell - 25°C)
produktion = (strålning / 1000) × area × verkningsgrad × PR × tempFactor
```

Vinden sitter i nämnaren: mer vind kyler panelen och höjer tempFactor. `tempFactor` kläms till intervallet [0.70, 1.10] för att skydda mot felaktiga sensorvärden.

Praktisk effekt på produktionsestimaten:

| Scenario | T_cell | tempFactor | Skillnad mot tidigare |
|---|---|---|---|
| Varm sommardag, vindstilla | ~61°C | 0.839 | −16% produktion |
| Varm sommardag med bris | ~51°C | 0.884 | −12% produktion |
| Kall vinterdag | ~11°C | 1.063 | +6% produktion |
| Typisk höstdag | ~29°C | 0.982 | −2% (nästan neutralt) |

Sommartid överskattade modellen produktionen med 13–16% eftersom cellerna hettar upp sig långt över 25°C STC. Det är nu korrigerat.

---

### Load shifting: /schedule-endpointen

`LoadScheduler` hittar det billigaste sammanhängande tidsfönstret för en given last via sliding window-algoritm. Input: energiprisschema, effekt i kW, tid i minuter, deadline. Output: optimal starttid, beräknad kostnad, besparing jämfört med att starta nu.

`ScheduleDB` lagrar planerade körningar i SQLite (`schedules`-tabellen). Soft delete — avbokning sätter status till `cancelled`.

Tre endpoints i API:et:
- `POST /schedule` — kör pipelinen, hitta billigaste fönstret, spara i DB
- `GET /schedule` — lista aktiva scheman för inloggad användare
- `DELETE /schedule/<id>` — avboka

---

### Infrastruktur

- JWT-validering med HMAC-SHA256 via mbedtls
- SQLite med schema-migrationer (`ALTER TABLE ADD COLUMN` ignorerar "duplicate column name")
- `scripts/seed_db.py` seedar testdata med Python-inbyggd sqlite3-modul
- `make dev` kör seed, startar server, testar forecast och kör ett EV-laddar-exempel automatiskt

---

## Kursmålstäckning just nu

| Kursmål | Beskrivning | Status |
|---|---|---|
| 1 | Processer, trådar, synkronisering, minne | ✅ Täckt |
| 2 | IPC — pipes, sockets, delat minne | ✅ Täckt |
| 3 | C vs C++ — syntax, abstraktion, resursmodell | ❌ Saknas |
| 4 | C++-objektmodell och RAII | ❌ Saknas |
| 5 | STL — vector, string, unique_ptr | ❌ Saknas |
| 6 | Profilering för prestandaoptimering | ❌ Saknas |
| 7 | Flertrådat program med effektiv synkronisering | ✅ Täckt |
| 8 | IPC-lösningar för processkommunikation | ✅ Täckt |
| 9 | C++-komponenter med RAII och STL | ❌ Saknas |
| 10 | Profilering — tolka resultat, identifiera flaskhalsar | ❌ Saknas |
| 11 | Optimering baserat på mätdata | ⚠️ Delvis |
| 12 | Dokumentera design, minnesmodeller, prestanda | ⚠️ Delvis |

**4 av 12 kursmål täckta fullt ut. Examination vecka 12.**

---

## Vad som saknas

### C++-klient (kursmål 3, 4, 5, 9)

Det finns inte en enda `.cpp`- eller `.hpp`-fil i projektet. `TODO.md` har sedan länge "KEVIN C++ (KLIENT / CLI / PLATFORM)" men den är aldrig gjord.

Utan C++-kod kan examinatorn inte bedöma kursmål 3–5 (skriftligt prov) eller kursmål 9 (projekt). Det är fyra kursmål av tolv — 33% av examinationen.

Vad klienten behöver demonstrera:

**Klasser (kursvecka 7):** En klass som representerar servern eller en HTTP-session. Konstruktor initierar resursen, destruktor frigör den.

**RAII (kursvecka 8):** Socket-wrapper som stänger socketen i destruktorn, oavsett om funktionen returnerar normalt eller vid exception. Inga `close()`-anrop utspridda i koden.

**STL (kursvecka 9):** `std::vector<ForecastEntry>` för att hålla forecast-data, `std::string` för HTTP-request och response-parsing, `std::unique_ptr` för något ägt objekt.

Det enklaste som täcker kursmålen är en CLI-klient som ansluter till servern, hämtar `/forecast` och skriver ut en tabell i terminalen. Behöver inte vara grafisk.

---

### Profilering (kursmål 6, 10, 11)

Kursvecka 10–11 handlar om att mäta med `gprof`, `perf` och `valgrind` — inte bara att koden är snabb. Kursmålet kräver att man *mäter*, tolkar data och dokumenterar vad man hittade.

Valgrind är det enklaste att börja med:

```bash
valgrind --leak-check=full --show-leak-kinds=all ./bin/GridGuard-server
```

Servern körs under watchdog i produktion, men för profilering startas den direkt utan daemon-läge. Kör ett par forecast-requests via curl och avsluta med SIGINT.

Callgrind visar vilka funktioner som tar mest CPU-tid:

```bash
valgrind --tool=callgrind ./bin/GridGuard-server
callgrind_annotate callgrind.out.<pid>
```

Resultatet av profileringen behöver dokumenteras — vad som hittades, om något optimerades som följd, varför. Det är det som täcker kursmål 10 och 11.

Kursmål 11 handlar specifikt om att optimera *baserat på mätdata*. Algoritm-förbättringarna vi gjort i Compute.c är bra men de gjordes utan mätdata — de täcker inte kursmålet formellt.

---

### Dokumentation (kursmål 12)

Changelogs och arkitekturdokument finns och är välskrivna. Det som saknas är det kursmålet specifikt nämner: *minnesmodeller*.

Det behöver inte vara ett stort dokument. Det räcker att beskriva:

- Vad som lever på stacken och i vilken tråd/process (t.ex. `WorkCompletion` stack-allokeras i HTTP-tråden, `FetchResult` stack-allokeras i fetcher-processen)
- Vad som lever i heap (t.ex. `worker`-objektet i compute-tråden, cJSON-noder)
- Vad som lever i delat minne (`SharedCacheRegion` via mmap, delas mellan server och fetcher)
- Livslängd på IPC-resurser (FIFO skapas vid GridGuard_Initiate, tas bort vid Shutdown)

Prestandadokumentationen skrivs efter profileringen — det är fel ordning att skriva den innan.

---

## Vad vi kan förbättra i koden

Dessa punkter är inte blockerande för examination men gör koden bättre:

**`localtime()` i Compute.c** är inte trådsäker (använder statisk buffer). Compute körs inuti en mutex, men det är en bräcklig garanti. Byt till `localtime_r()` och lägg till `#define _POSIX_C_SOURCE 200809L` i Compute.c.

**`waitpid()` på child-processerna** saknas i `GridGuard_Shutdown()`. Fetcher och parser avslutas via `kill(SIGTERM)` men main-processen väntar aldrig på att de faktiskt dött. Det lämnar zombie-processer tills watchdog-processen skördas. Lägg till `waitpid(app->fetchPid, NULL, 0)` och `waitpid(app->parserPid, NULL, 0)` efter kill-anropen.

**Inga enhetstester för LoadScheduler.** `LoadScheduler_FindWindow()` är algoritmisk kod som är enkel att testa isolerat med mockad indata. Utan tester är det svårt att veta om edge cases som "alla timmar är lika dyra" eller "deadline är passerad" beter sig rätt.

**`cJSON`-biblioteket** har ett känt O(n²)-problem i sin strlen-användning (noterat med TODO i källkoden). Det spelar ingen roll för 96-timmars forecast, men om forecast-fönstret någonsin utökas märks det.

**Förbrukningsprofilen är platt.** `consumptionKwh` är ett konstant värde per timme. En verklig frukosttimme förbrukar 2–3× mer än en natten-timme. En SELL-signal kl 07:00 kan i praktiken vara nettoimport om hushållet frukostlagar. Det kräver att kunden antingen anger en timbaserad förbrukningsprofil eller att systemet lär sig från historik — det är en större förändring.

---

## Prioriteringsordning

**1. C++-klienten** — blockerande för 33% av examinationen. Skriv den innan vecka 12.

**2. Profilering** — kör valgrind och callgrind, dokumentera resultaten. Tar ett par timmar om systemet redan fungerar.

**3. Minnesmods-dokumentation** — skriv ett kort dokument som beskriver stack/heap/shared memory-användningen i systemet.

**4. `waitpid()` för child-processer** — liten fix, eliminerar zombie-processer vid shutdown.

**5. `localtime_r()`** — liten fix, korrektare kod.

---

**Skapat:** 2026-03-02
**Baserat på:** Kodbas-analys + Kursplanering.pdf
