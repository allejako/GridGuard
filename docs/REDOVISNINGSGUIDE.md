# GridGuard - Redovisningsguide

**Tips:** Öppna detta dokument i en split-view bredvid IDE:t. Följ strukturen uppifrån och ner.

---

## Systemöversikt (börja här)

**Vad är GridGuard?**
- Energiplanerings-API för hem med solpaneler
- Hämtar spotpriser och väderdata
- Beräknar optimal BUY/SELL/IDLE-plan per timme
- REST API med JWT-autentisering

**Arkitektur:**
- 3 processer: Server, Fetcher, Parser
- Multi-process IPC (inte trådar för allt)
- Demonstrerar alla kursmål från vecka 1-5

**Databas:** SQLite (användarkonfiguration)
**API:er:** Open-Meteo (väder), Elpriset.se (spotpris)

---

## Demo-flöde (börja alltid med make dev)

```bash
make dev
```

**Vad händer:**
1. Bygger alla tre binärer
2. Startar watchdog som startar servern
3. Seedar testdata
4. Kör test-request och visar JSON-svar

**Visa processerna:**
```bash
ps aux | grep GridGuard
```
Tre processer: server, fetcher, parser

**Visa IPC-resurser:**
```bash
ls -lh /tmp/gridguard*
```
Anonymous pipe (intern), FIFO, Unix socket, PID-filer

---

## Processarkitektur (förklara big picture först)

**src/processes/README.md** ← Börja här, visa diagrammet

### Tre processer:

**1. GridGuard-server (main process)**
- HTTP-server med ThreadPool (20 workers)
- Compute-thread för beräkningar
- Spawnar Fetcher och Parser med fork+exec

**2. GridGuard-fetcher**
- Läser WorkRequest från stdin (anonymous pipe)
- Hämtar väderdata och spotpriser via HTTP
- Cachar i shared memory
- Skriver FetchResult till FIFO

**3. GridGuard-parser**
- Läser FetchResult från FIFO
- Parsar JSON från API:erna
- Lyssnar på Unix domain socket
- Skickar ParseResult när Compute ansluter

### IPC-flödet:

```
HTTP Request
  ↓ (WorkRequest)
Anonymous Pipe (fork+dup2)
  ↓
Fetcher Process
  ↓ (FetchResult)
Named FIFO (mkfifo)
  ↓
Parser Process
  ↓ (ParseResult via Unix socket)
Compute Thread
  ↓
HTTP Response
```

---

## Kursmål-mappning (viktigt för examination!)

### Vecka 1: Processhantering
**Filer att visa:**
- `src/application/core/GridGuard.c` (rad 105-175)
  - `fork()` för Fetcher (rad 106)
  - `execl()` med dynamisk path via `/proc/self/exe` (rad 138)
  - `fork()` för Parser (rad 148)
  - `waitpid()` i shutdown (visa Shutdown-funktionen)

**Poäng att nämna:**
- Varje process har egen `main()` i `src/processes/*/main.c`
- Fristående binärer, inget delat minne

### Vecka 2-3: Threads och synkronisering
**Filer att visa:**
- `src/concurrency/threads/ThreadPool.c`
  - 20 HTTP-workers med work-queue-modell
  - Alla blockerar på `Queue_Pop()` (visa rad ~50-60)

- `src/concurrency/sync/CompletionRegistry.c`
  - Mutex/cond för att vänta på svar från pipeline
  - HTTP-worker sover tills Compute signalerar

**Poäng att nämna:**
- ThreadPool använder pthread_create/join
- CompletionRegistry använder pthread_mutex_t och pthread_cond_t
- Work-queue istället för select() per worker

### Vecka 4: Pipes och FIFO
**Filer att visa:**
- `src/application/core/GridGuard.c` (rad 92-103)
  - `pipe()` skapar anonymous pipe
  - `dup2()` omdirigerar stdin till pipe read-end (rad 126-130)
  - HTTP skriver WorkRequest, Fetcher läser från stdin

- `src/application/core/GridGuard.c` (rad 75-86)
  - `mkfifo()` skapar named pipe
  - Fetcher skriver, Parser läser

**Filer att visa (användning):**
- `src/processes/fetcher/fetcher.c` (rad 121): läser från STDIN_FILENO
- `src/processes/fetcher/fetcher.c` (rad 237): `write()` till FIFO
- `src/processes/parser/parser.c` (rad 127): `open()` FIFO för läsning

**Poäng att nämna:**
- Anonymous pipe för parent→child (via fork+dup2)
- Named FIFO för oberoende processer (Fetcher→Parser)

### Vecka 5: Unix sockets och shared memory
**Filer att visa:**
- `src/processes/parser/parser.c` (rad 133-172)
  - `socket(AF_UNIX, SOCK_STREAM)` (rad 133)
  - `bind()` till `/tmp/gridguard_parse_to_compute.sock` (rad 153)
  - `listen()` och `accept()` när Compute ansluter

- `src/application/workers/ComputeWorkerHybrid.c` (rad 44-77)
  - Compute är klient: `socket()` och `connect()`
  - Läser ParseResult från socket

**Shared memory:**
- `src/infrastructure/cache/SharedCache.c`
  - `shm_open()` för att skapa `/gridguard_weather` och `/gridguard_price`
  - `mmap()` för att mappa in i adressrymd
  - `sem_open()` för synkronisering mellan Fetcher-processer

**Poäng att nämna:**
- Parser är socket-server, Compute är socket-klient
- Shared memory överlever process-restart
- Semaforer synkroniserar access till cache

---

## Request-flöde (hoppa mellan filer under demo)

### 1. HTTP Request kommer in
**Fil:** `src/server/Server.c` (rad 60)
- TCP-server tar emot på port 8080
- ThreadPool tar hand om connection

### 2. HTTP parsing
**Fil:** `src/network/http/HTTPRequest.c`
- Parsar method, path, headers
- Extraherar JWT från Authorization-header

### 3. JWT-validering
**Fil:** `src/infrastructure/auth/JWTValidator.c`
- Validerar signatur med HMAC-SHA256 (mbedtls)
- Kontrollerar att token inte gått ut
- Extraherar `sub` (user_id)

### 4. Användarkonfiguration
**Fil:** `src/infrastructure/database/UserConfigDB.c`
- SQLite-query: `SELECT * FROM user_configs WHERE user_id = ?`
- Returnerar lat, lon, region, solpanel-area, effektivitet

### 5. WorkRequest skapas
**Fil:** `src/server/ClientHandler.c` (rad 45-70)
- Fyller struct med användardata
- Registrerar i CompletionRegistry för att vänta på svar

### 6. Skriver till pipe
**Fil:** `src/application/core/GridGuard.c` (rad 236-250)
- `write()` till pipe → Fetcher får det på stdin

### 7. Fetcher hämtar data
**Fil:** `src/processes/fetcher/fetcher.c` (rad 159-245)
- Kollar cache först (SharedCache)
- Om MISS: HTTP-requests till Open-Meteo och Elpriset
- Lagrar i cache med TTL
- Skriver FetchResult till FIFO

### 8. Parser läser och parsar
**Fil:** `src/processes/parser/parser.c` (rad 195-245)
- Läser FetchResult från FIFO
- Parsar JSON med cJSON
- Väntar på socket-connection från Compute

### 9. Compute genererar plan
**Fil:** `src/application/services/Compute.c` (rad 47-120)
- Läser ParseResult från socket
- Beräknar solproduktion per timme
- Jämför mot spotpris och sätter BUY/SELL/IDLE

### 10. HTTP Response
**Fil:** `src/server/ClientHandler.c` (rad 100-120)
- HTTP-worker vaknar (CompletionRegistry signalerar)
- Bygger JSON-svar
- Skickar via `HTTPResponse_SendJSON()`

---

## Viktiga datastrukturer (visa headers)

### WorkRequest
**Fil:** `src/application/core/GridGuard.h` (rad 14-27)
- userId, location, lat/lon, region
- solarAreaM2, solarEfficiency, consumptionKwh
- grid_fee_low/normal/high (nätavgifter)

### FetchResult
**Fil:** `src/processes/fetcher/fetcher.h` (rad 18-46)
- userId, location, region
- openMeteoJson (väderdata som string)
- elprisetJson (spotpriser som string)
- grid fees kopieras vidare

### ParseResult
**Fil:** `src/processes/parser/parser.h` (rad 34-43)
- userId, location, region
- ForecastData (96 timmar väder + pris)
- grid fees och solpanel-config

### ForecastData
**Fil:** `src/application/models/domain/Energy.h` (rad 30-50)
- Array med 96 entries
- Varje entry: timestamp, spotPris, irradiance, temp

---

## Databas-schema (öppna gridguard.db i sqlite3)

```bash
sqlite3 gridguard.db
.schema user_configs
```

**Tabell: user_configs**
- user_id (TEXT PRIMARY KEY) ← från JWT sub
- latitude, longitude, region
- solar_area_m2, solar_efficiency
- consumption_kwh (standard 1.5 kWh/h)
- grid_fee_low/normal/high (Ellevio Tid3-tariff)
- updated_at (UNIX timestamp)

**Filer:**
- `src/infrastructure/database/Database.c` - öppnar SQLite med FULLMUTEX
- `src/infrastructure/database/UserConfigDB.c` - Get/Upsert operationer

---

## Makefile-targets (demonstrera)

```bash
make           # Bygger alla binärer
make dev       # Startar system + kör test
make stop      # Stoppar alla processer
make test      # Kör unit tests
make clean     # Rensar build-artefakter
```

**Visa:** Makefile (rad 179-237)
- Tre separata link-targets för tre binärer
- Watchdog target (rad 251-257)

---

## Felhantering och robusthet

### Zombie-processen
**Förklara problemet:**
- Daemon byter cwd till `/`
- `execl("./bin/GridGuard-fetcher")` letade i `/bin/`
- Child dog direkt, blev zombie

**Lösningen:** `src/application/core/GridGuard.c` (rad 37-52)
- `readlink("/proc/self/exe")` för att hitta egen path
- `dirname()` extraherar bin-katalogen
- Dynamiskt konstruerade paths → fungerar överallt

### Watchdog
**Fil:** `src/infrastructure/watchdog/Watchdog.c`
- Övervakar server-process med heartbeat
- Startar om automatiskt vid krasch
- Timeout efter 15 sekunder utan heartbeat

---

## Tester (om tid finns)

**Unit tests:**
```bash
make test
```

**Viktiga tester:**
- `test_jwt_validator` - JWT-validering
- `test_http_request` - HTTP parsing med socketpair
- `test_pipeline` - End-to-end genom hela systemet

**Fil:** `src/tests/integration/test_pipeline.c`
- Kör hela flödet från WorkRequest till färdig plan

---

## Optimering och profiling (om tid finns)

**Shared memory cache:**
- Undviker att hämta samma API-data flera gånger
- TTL: 15 min för väder, 24h för spotpriser
- Delas mellan alla Fetcher-instanser

**ThreadPool work-queue:**
- O(1) access till jobb
- Ingen select() overhead per worker
- HTTP-connection hanteras av en worker från start till slut

---

## Avslutning (sammanfatta)

**Täckta kursmål:**
- ✓ Vecka 1: fork, exec, waitpid
- ✓ Vecka 2-3: pthreads, mutex, cond
- ✓ Vecka 4: anonymous pipes, named pipes (FIFO)
- ✓ Vecka 5: Unix sockets, shared memory, semaforer
- ✓ JWT-autentisering (mbedtls)
- ✓ SQLite-databas
- ✓ HTTP REST API

**Nästa steg:**
- C++-klient (kursmål 9: RAII, STL, klasser)
- Profiling med gprof/valgrind (kursmål 10-11)

**Projektstruktur:**
- Clean separation: application, infrastructure, network, concurrency
- Varje process fristående och testbar
- Dokumenterad i changelogs och README

---

## Quick reference (om frågor kommer)

**Varför inte trådar för allt?**
→ Kursmål 2 och 8 kräver IPC mellan processer, inte bara shared memory

**Varför tre processer?**
→ Demonstrera alla IPC-typer: pipes, FIFO, sockets, shared memory

**Varför shared memory OCH sockets?**
→ Parser behöver veta när data är färdig (socket), men cache delas mellan anrop (shm)

**Hur funkar JWT-validering utan databas?**
→ Stateless: servern validerar signaturen med delad hemlig nyckel

**Vad händer vid server-restart?**
→ Shared memory cache överlever (persistent), pipes/sockets skapas på nytt

**Är det production-ready?**
→ Nej, men visar alla kursmål. Saknar: error recovery, rate limiting, TLS
