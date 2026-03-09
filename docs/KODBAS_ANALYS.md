# GridGuard — Professionell Kodbasanalys

> **Datum:** 2026-03-09
> **Underlag:** Kursplanering.pdf (Systemprogrammering och introduktion till C++, 60 yhp, 12 veckor) + BoilerRoomProjekt.pdf (LEOP — Local Energy Optimization Platform)
> **Kursvecka vid analystillfälle:** ~9 av 12

---

## Innehåll

1. [Systemöversikt](#1-systemöversikt)
2. [Kursmål — täckningsanalys](#2-kursmål--täckningsanalys)
3. [Arkitekturanalys](#3-arkitekturanalys)
4. [Fil- och mappstruktur — kritisk genomgång](#4-fil--och-mappstruktur--kritisk-genomgång)
5. [Kodkvalitet — konkreta fynd](#5-kodkvalitet--konkreta-fynd)
6. [Saknade leverabler](#6-saknade-leverabler)
7. [Rekommenderad målstruktur (Pipeline-First)](#7-rekommenderad-målstruktur-pipeline-first)
8. [Prioriterad åtgärdslista](#8-prioriterad-åtgärdslista)

---

## 1. Systemöversikt

GridGuard implementerar en lokal energioptimeringsenhet (LEOP) i C/C++ med följande processarkitektur:

```
HTTP-klient (C++)
      │  TCP :8080
      ▼
┌─────────────────────────────────────────────────────┐
│  GridGuard-server (main process)                    │
│  ┌──────────┐  ThreadPool (20 workers)              │
│  │ TCPServer│──► ClientHandler ──► JWT-gate          │
│  └──────────┘         │                             │
│              WorkCompletion (stack, per request)     │
│                        │ pipe (stdin)               │
│                        ▼                            │
│         ┌─── ComputeWorker (thread) ◄──┐            │
│         │    Unix socket              │            │
│         │                             │            │
└─────────┼─────────────────────────────┼────────────┘
          │ fork+exec                   │ fork+exec
          ▼                             ▼
  GridGuard-fetcher            GridGuard-parser
  (separat process)            (separat process)
       │                             │
  POSIX shm                    Unix domain socket
  (weather/price cache)        → skickar ParseResult
       │                         till ComputeWorker
       │ named pipe (FIFO)
       └──────────────────► GridGuard-parser
```

**IPC-kedja:**
```
WorkRequest (pipe stdin)
  → Fetcher: HTTP-anrop + SharedCache (shm + POSIX-semafor)
  → FIFO (named pipe): FetchResult
  → Parser: JSON-parsning + struct-byggande
  → Unix domain socket: ParseResult
  → ComputeWorker (thread): energiplan → JSON → WorkCompletion
  → HTTP-svar till klient
```

**Separata binärer:**
| Binär | Språk | Roll |
|---|---|---|
| `GridGuard-server` | C | Huvudprocess: HTTP + compute |
| `GridGuard-fetcher` | C | Datahämtning (API + cache) |
| `GridGuard-parser` | C | JSON-parsning + datamodellering |
| `GridGuard-watchdog` | C | Processövervakning + omstart |
| `GridGuard-client` | C++ | CLI-klient |

---

## 2. Kursmål — täckningsanalys

### Kunskaper (1–6)

| # | Kursmål | Status | Var i koden |
|---|---|---|---|
| 1 | Förklara hur OS hanterar processer, trådar, synkronisering och minne | ✅ **Täckt** | `fork()`+`exec()` i GridGuard.c; `pthread_create` i WorkerPool; `pthread_mutex`+`pthread_cond` i Queue.c och Compute.c |
| 2 | Redogöra för IPC: pipes, sockets, delat minne | ✅ **Täckt** | Named pipe (FIFO) fetcher→parser; Unix domain socket parser→compute; POSIX shm + `sem_open` i SharedCache |
| 3 | Förklara skillnader mellan C och C++ | ✅ **Täckt** | C-server + C++-klient i samma repo; kommentarer i Makefile om RAII/STL/namespaces |
| 4 | Redogöra för C++-objektmodellen och RAII | ✅ **Täckt** | `std::unique_ptr<HttpClient>` i GridGuardClient; `SocketGuard.hpp` (RAII för fd); destruktorer |
| 5 | Förklara hur STL-komponenter hanterar resurser | ✅ **Täckt** | `std::vector`, `std::map`, `std::sort`, `std::min_element`, `std::accumulate`, `std::count_if` i client/main.cpp |
| 6 | Förklara hur profilering används för prestandaoptimering | ⚠️ **Otillräckligt** | Makefile har `make profile` (gprof) och `make valgrind-server` men **inget profileringsresultat finns dokumenterat** |

### Färdigheter (7–12)

| # | Kursmål | Status | Var i koden |
|---|---|---|---|
| 7 | Implementera flertrådat C/C++ med effektiv synkronisering | ✅ **Täckt** | ThreadPool (20 workers), Queue (bounded, mutex+cond), WorkCompletion (cond-baserad barrier), CompletionRegistry |
| 8 | Använda IPC-lösningar för processkommunikation | ✅ **Täckt** | FIFO + Unix socket + shm; se IPC-kedjan ovan |
| 9 | Implementera C++-komponenter med RAII och STL | ✅ **Täckt** | SocketGuard, unique_ptr, move-semantik i GridGuardClient-konstruktor |
| 10 | Utföra profilering, tolka resultat, identifiera flaskhalsar | ❌ **Saknas** | Ingen profileringsrapport, inga mätvärden, ingen dokumenterad flaskhals |
| 11 | Optimera kod baserat på mätdata och resursanalys | ❌ **Saknas** | Inga före/efter-mätningar. `qsort` i Compute.c är ett bra optimeringsobjekt men ej dokumenterat |
| 12 | Dokumentera design, minnesmodeller och prestandaöverväganden | ⚠️ **Delvis** | Många interna docs-filer men ingen enhetlig, examinationsklar arkitekturdokumentation med minnesmodell |

### Sammanfattning kursmål

```
Täckta (klara):      1, 2, 3, 4, 5, 7, 8, 9     → 8/12
Delvis täckta:       6, 12                         → 2/12
Saknas helt:         10, 11                         → 2/12
```

**Kritisk notering:** Kursmål 10 och 11 är direkt kopplade till examinationskravet "profileringsrapport med före/efter-mätningar" som är ett obligatoriskt leveranskrav. Detta måste åtgärdas innan vecka 12.

---

## 3. Arkitekturanalys

### 3.1 Styrkor

**Genuint systemprogrammering på rätt nivå**

Projektet demonstrerar verkliga systemnära koncept — inte bara simulerade:

- `fork()` + `execv()` med korrekt `waitpid()` och SIGCHLD-hantering
- Named FIFO med blockerande `open()` som synkroniseringspunkt (fetcher väntar på parser)
- POSIX shared memory (`shm_open`, `mmap`, `ftruncate`) med named semaforer (`sem_open`) — inte bara in-memory struct
- `sigaction()` med `SA_RESTART`-mönster; `SIGPIPE` ignoreras korrekt
- Daemon-mönster med setsid, fd-stängning och PID-fil
- Watchdog med heartbeat-pipe (GRIDGUARD_HEARTBEAT_FD) — elegant lösning

**Compute-motorn är genuint välgjord**

`Compute.c` implementerar en seriös fysikalisk modell:
- NOCT-temperaturmodell (IEC 61215/61724) med vind-kylningskoefficient
- Temperatur-derating med korrekt klamring (0.70–1.10)
- 3-pass arkitektur: kostnadsberäkning → percentiltröskel → BUY/SELL/IDLE-beslut
- Välkommenterade konstanter med industrireferenser
- `qsort` för percentilberäkning (30:e percentilen → BUY-signal)

**C++-klienten visar genuint C++-tänk**

- `std::unique_ptr<HttpClient>` med `std::make_unique` (RAII, no raw `new`)
- Move-semantik i konstruktorn: `token_(std::move(token))`
- STL-algoritmer: `std::min_element`, `std::accumulate`, `std::count_if`, `std::sort`
- `std::iomanip` för tabellformattering
- Lambdas som jämförare
- `gridguard`-namespace genomgående

**ThreadPool-designen är korrekt**

Work-queue-modellen med `Queue_Pop()` som lastbalansering är det rätta valet. Varje HTTP-worker blockerar på kön, plockar en fd, hanterar hela request-livscykeln och återgår. Inga race conditions i grundflödet.

### 3.2 Arkitekturproblem

**Problem 1: Tight coupling i IPC-protokollet**

`FetchResult`- och `ParseResult`-strukturerna skickas som råa binärblobs via FIFO och Unix socket:

```c
// fetcher.c rad 205
ssize_t written = write(proc->fifoFd, &result, sizeof(result));

// parser.c rad 251
ssize_t written = write(clientSocket, &parseResult, sizeof(parseResult));
```

Detta bryter om någon ändrar struct-layouten (ny fält, ändrad storlek). Protokollet saknar version/magic number. Om fetcher och parser byggs vid olika tillfällen kan ABI-inkompatibilitet uppstå tyst.

**Problem 2: Parser accepterar exakt en anslutning per FIFO-läsning**

Parser-logiken är sekventiell:
1. Läs FetchResult från FIFO
2. Parsera JSON
3. `accept()` — vänta på ComputeWorker
4. Skriv ParseResult
5. Gå till 1

Om ComputeWorker inte hinner ansluta innan nästa FIFO-läsning, eller om två requests körs parallellt, kommer en av dem att blockera eller tappa sin anslutning. Systemet är i praktiken single-threaded per pipeline-körning trots thread pool.

**Problem 3: `parse_iso8601` i parser.c använder fel tid-funktion**

```c
// parser.c rad 26-30 — BUG
static time_t parse_iso8601(const char *timeStr)
{
    struct tm tm = {0};
    sscanf(timeStr, "%d-%d-%dT%d:%d:%d", ...);
    return mktime(&tm);  // ← tolkar som LOKAL tid, inte UTC!
}
```

`mktime()` tolkar `struct tm` som lokal tid. API:erna (Open-Meteo, Elpriset) returnerar UTC. I Sverige (UTC+1/+2) innebär detta att alla timestamp-matchningar i `build_forecast_data()` är 1-2 timmar fel. Spotpris matchas mot fel vädertimme. `ClientHandler.c` gör detta rätt med `timegm()` — samma fix behövs i parser.c.

**Problem 4: Logisk bugg i "no prices"-fallet**

```c
// parser.c rad 230-238
if (pricesParsed)
    build_forecast_data(&omData, &elprisetData, ...);
else
    build_forecast_data(&omData, &elprisetData, ...);  // identisk anrop!
```

Båda grenarna anropar exakt samma funktion med exakt samma argument. Intentionen var förmodligen att hantera fallback (noll-prissätta alla timmar eller skippa), men koden gör inget annat än att upprepa sig. Spotpriserna blir 0.0 för alla timmar om elpriset-parsningen misslyckas — men detta kommuniceras inte till klienten.

**Problem 5: `platform/` kompileras in i fel binary**

`src/platform/` är *avsiktlig* — den demonstrerar arkitekturens separationen (CHANGELOG_2026-03-03). `generate_jwt.py` kompilerar en C-wrapper som använder `build/platform/auth/JWTIssuer.o` direkt för att visa att JWT-utfärdning sker från plattformssidan med samma kod.

Problemet är att Makefilen inkluderar dessa filer i `SERVER_SRCS_C`:

```makefile
# Makefile rad 154-155 — fel placering
SERVER_SRCS_C = ...
                $(PLATFORM_AUTH_DIR)/JWTIssuer.c \   ← länkas in i servern
                $(PLATFORM_DB_DIR)/PlatformDB.c \     ← länkas in i servern
```

`JWTIssuer_CreateToken()` och `PlatformDB_*()` anropas **aldrig** från servern vid runtime — servern *validerar* JWT via `JWTValidator`, den utfärdar dem inte. Koden är dead code i server-binären, men den *motbevisar* den arkitektoniska poängen: om enheten kompilerar in JWT-utfärdningskoden kan den i teorin också utfärda tokens.

**Bättre lösning:** Lägg till en separat Makefile-regel som kompilerar plattformens `.o`-filer utan att länka dem in i servern. `generate_jwt.py` behöver bara att `.o`-filerna *existerar* i `build/` — det spelar ingen roll om de är en del av server-binären eller inte.

```makefile
# Lägg till i Makefile — kompilerar platform-objekt utan att länka dem i servern
.PHONY: platform-objects
platform-objects: directories
    $(CC) $(CFLAGS) -c $(PLATFORM_AUTH_DIR)/JWTIssuer.c \
        -o $(BUILD_DIR)/platform/auth/JWTIssuer.o
    $(CC) $(CFLAGS) -c $(PLATFORM_DB_DIR)/PlatformDB.c \
        -o $(BUILD_DIR)/platform/database/PlatformDB.o

# Uppdatera 'all:' att inkludera platform-objects
all: directories $(SERVER_BIN) $(FETCHER_BIN) $(PARSER_BIN) $(CLIENT_BIN) platform-objects
```

Och ta bort raderna 154-155 ur `SERVER_SRCS_C`. Resultatet: server-binären innehåller noll JWT-utfärdningskod, vilket *faktiskt demonstrerar* separationen bättre än nuläget. `generate_jwt.py` fortsätter fungera identiskt.

---

## 4. Fil- och mappstruktur — kritisk genomgång

### 4.1 Nuvarande struktur (förenklad)

```
src/
├── main.c
├── application/
│   ├── api/              ← APIEndpoints, APIParser
│   ├── configs/          ← Config.h
│   ├── core/             ← Server, ClientHandler, GridGuard
│   ├── models/
│   │   ├── apis/         ← ElprisetResponse, OpenMeteoResponse
│   │   ├── config/       ← UserConfig
│   │   ├── domain/       ← Energy, Forecast, SpotPrice, Weather
│   │   └── ipc/          ← FetchResult, ParseResult, WorkRequest ← FEL PLATS
│   └── services/         ← Compute, LoadScheduler
├── client/               ← C++-klient
├── concurrency/
│   ├── sync/             ← Queue, WorkCompletion, CompletionRegistry
│   └── threads/          ← ThreadPool, WorkerPool, ComputeWorker
├── database/             ← ClientDB, ScheduleDB, UserConfigDB ← DUBBLERING
├── infrastructure/
│   ├── auth/             ← JWTValidator
│   ├── cache/            ← SharedCache
│   ├── daemon/           ← Daemon, PidFile
│   ├── database/         ← Database, UserConfigDB ← DUBBLERING
│   ├── logging/          ← Logger
│   ├── processes/
│   │   ├── fetcher/      ← fetcher.c/h, main.c
│   │   ├── parser/       ← parser.c/h, main.c
│   │   └── watchdog/     ← Watchdog.c/h, main.c
│   └── signals/          ← SignalHandler
├── libs/                 ← cJSON
├── network/
│   ├── client/           ← HTTPClient, HTTPFetcher
│   ├── http/             ← HTTPRequest, HTTPResponse
│   └── tcp/              ← TCPServer
├── platform/             ← JWTIssuer, PlatformDB ← HÖR EJ HIT
└── tests/
    ├── integration/
    └── unit/
```

### 4.2 Identifierade strukturproblem

| # | Problem | Allvarlighetsgrad | Beskrivning |
|---|---|---|---|
| 1 | `src/database/` vs `src/infrastructure/database/` | 🔴 Kritisk | Två mappar med delvis samma filer (`UserConfigDB` finns i båda). Oklar auktoritär källa. |
| 2 | `src/platform/` | 🔴 Kritisk | JWTIssuer + PlatformDB hör till plattformsservern, inte enheten. Kompileras ändå in i servern. |
| 3 | IPC-structs i `application/models/ipc/` | 🟡 Allvarlig | `WorkRequest`, `FetchResult`, `ParseResult` är IPC-kontrakt — inte domänmodeller. Hör nära IPC-koden. |
| 4 | `application/core/` innehåller servercode | 🟡 Allvarlig | `Server.c`, `ClientHandler.c`, `GridGuard.c` är serverspecifik kod, inte generell "core". |
| 5 | 15+ `-I` flaggor i Makefile | 🟡 Allvarlig | Varje undermapp är en egen include-path. Symptom på för djup nästling. Gör det möjligt att inkludera headers utan att referera till sin path, vilket döljer beroenden. |
| 6 | `build/` speglar inte `src/` | 🟠 Måttlig | Build-artefakter i `build/application/workers/`, `build/server/`, `build/database/` etc. som inte matchar nuvarande `src/`-struktur — kvarleva från tidigare refaktoreringssteg. |
| 7 | `application/workers/` saknas i `src/` men finns i `build/` | 🟠 Måttlig | `ComputeWorker` har tydligen flyttats utan att gamla build-artefakter rensats. |
| 8 | `gridguard.db`, `platform.db`, `logs/*.log`, `bin/*` i repo | 🟠 Måttlig | Runtime-artefakter ska inte commitas. `.gitignore` är otillräcklig. |
| 9 | Ingen `README.md` | 🔴 Kritisk | Obligatoriskt leveranskrav enligt BoilerRoomProjekt.pdf. Saknas helt. |

### 4.3 Makefile-komplexitet

Makefilen är 658 rader och definierar 15 separata inkluderingsvägar:

```makefile
INCLUDES = -I$(SRC_DIR) -I$(LIBS_DIR) -I$(APPLICATION_DIR) -I$(APP_CORE_DIR)
           -I$(APP_WORKERS_DIR) -I$(FETCHER_DIR) -I$(PARSER_DIR)
           -I$(APP_MODELS_DIR) ...  # (fortsätter 11 rader till)
```

Varje ny undermapp kräver en ny `-I` flagga. Det gör att headers kan inkluderas utan att ange sin relativa path, vilket döljer vilka moduler som beror på varandra. En platt struktur med 5-6 toppnivåmappar hade reducerat detta till 5-6 rader.

---

## 5. Kodkvalitet — konkreta fynd

### 5.1 Positiva fynd

**Korrekt atomär signal-variabel:**
```c
// SignalHandler.c
static volatile sig_atomic_t keep_running = 1;
// ✅ Rätt typ för signal-handler-kommunikation
```

**Korrekt `pthread_cond_wait`-mönster med loop:**
```c
// Queue.c
while (queue->count == 0 && !queue->isShutdown)
    pthread_cond_wait(&queue->notEmpty, &queue->mutex);
// ✅ Loop skyddar mot spurious wakeups
```

**Korrekt tid-hantering i ClientHandler:**
```c
// ClientHandler.c
extern time_t timegm(struct tm *tm);  // explicit extern för glibc-extension
// ✅ Använder timegm() för UTC-korrekt parsing
```

**Korrekt `strncpy` med null-terminering:**
```c
strncpy(proc->fifoPath, fifoPath, sizeof(proc->fifoPath) - 1);
// ✅ Reserverar plats för \0
```

**Signal-safe skrivning i handler:**
```c
static void SignalHandler_Write(const char *msg) {
    write(STDOUT_FILENO, msg, strlen(msg));  // ✅ write() är async-signal-safe
}
```

### 5.2 Negativa fynd / buggar

**Bug 1: `mktime()` vs `timegm()` i parser.c**
```c
// parser.c:26-30 — BUGG
static time_t parse_iso8601(const char *timeStr) {
    struct tm tm = {0};
    sscanf(timeStr, "%d-%d-%dT%d:%d:%d", ...);
    return mktime(&tm);  // ❌ LOKAL tid — bör vara timegm() för UTC
}
```
*Konsekvens:* Alla timestamp-jämförelser i `build_forecast_data()` är systematiskt 1-2 timmar fel i Sverige. Spotpriset matchas mot fel vädertimme. Energiplanen blir fel.

**Bug 2: Identiska else-grenar i parser.c**
```c
// parser.c:230-238 — LOGISK BUGG
if (pricesParsed)
    build_forecast_data(&omData, &elprisetData, fetchResult.region, &parseResult.forecastData);
else
    build_forecast_data(&omData, &elprisetData, fetchResult.region, &parseResult.forecastData);
    //                  ↑ identiska anrop — else-grenen gör ingenting annat
```
*Konsekvens:* Fallback-logiken (tom `elprisetData` → spotpris = 0.0 för alla timmar) kommuniceras inte. Klienten får en energiplan utan pristinformation utan varning.

**Bug 3: Potentiell buffer overflow i `HandleGetSchedules`**
```c
// ClientHandler.c:468
char buf[SCHEDULE_MAX_PER_USER * 300 + 32];
```
Om `SCHEDULE_MAX_PER_USER` ökas utan att ta hänsyn till faktisk max JSON-längd per entry kan bufferten vara för liten. Nuvarande 300 bytes per entry är lagom men ej motiverat med en kommentar.

**Kvalitetsproblem 1: Binär struct-serialisering över IPC**
```c
write(proc->fifoFd, &result, sizeof(result));  // FetchResult som råbytes
```
- Inget versionshantering
- Bryter vid recompilering med ändrad struct-storlek
- Plattformsberoende (padding, endianness)
- Svårt att debugga (kan inte läsas som text)

**Kvalitetsproblem 2: 32 KB JSON-buffer i FetchResult**
```c
// FetchResult.h (via SharedCache.h)
#define SHARED_CACHE_DATA_MAX 32768  // 32 KB
char openMeteoJson[32768];
char priceJson[32768];
```
`FetchResult` är ca 65+ KB stor och skickas i sin helhet via FIFO. Det funkar men är minnesintensivt. En pipe-baserad streaming-lösning eller delat minne (som redan finns!) vore lämpligare.

**Kvalitetsproblem 3: `sscanf` utan returvärde-kontroll**
```c
// parser.c:26
sscanf(timeStr, "%d-%d-%dT%d:%d:%d",
       &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
       &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
// ❌ Returvärdet från sscanf kontrolleras inte (bör vara 6)
```

**Kvalitetsproblem 4: `abs()` på `difftime()` (double)**
```c
// parser.c:59
if (abs((int)difftime(entry->timestamp, priceTime)) < 60)
```
`difftime()` returnerar `double`. Castning till `int` tappar precision. `abs()` är för `int`; bör använda `fabs()` på `double`. För tidsskillnader nära 60 sekunder kan detta ge fel.

### 5.3 `.gitignore` saknar viktiga poster

Följande filer är committade till repot men borde ignoreras:

```
# Saknas i .gitignore:
gridguard.db
platform.db
logs/*.log
bin/
build/
*.o
gmon.out
profile_report.txt
```

---

## 6. Saknade leverabler

Enligt BoilerRoomProjekt.pdf ska följande levereras vid kursvecka 12:

| Leverabel | Status | Notering |
|---|---|---|
| Komplett källkod i Git-repository | ✅ Finns | |
| Makefile för kompilering | ✅ Finns | |
| Fungerande server och klient | ✅ Finns | |
| Konfigurationsfiler med dokumentation | ⚠️ Delvis | `Config.h` finns men ingen extern konfigurationsfil (t.ex. JSON/INI) |
| Automatiserade tester | ✅ Finns | 7 test-targets |
| **README med installationsinstruktioner** | ❌ **Saknas** | Obligatoriskt — finns ingen README.md alls |
| **Arkitekturdokumentation med diagram** | ⚠️ Delvis | Många .md-filer i docs/ men ingen samlad, aktuell arkitekturdok |
| API-dokumentation | ❌ **Saknas** | Ingen dokumentation av REST-endpointernas kontrakt |
| **Profileringsrapport med före/efter-mätningar** | ❌ **Saknas** | Kritiskt för kursmål 10-11 |
| Individuell skriftlig reflektion | ❌ **Saknas** | Per student — personligt |

---

## 7. Rekommenderad målstruktur (Pipeline-First)

Följande struktur löser alla identifierade problem och speglar kursens pipeline-krav explicit:

```
src/
├── main.c                    # Server entry point — 70 rader
│
├── pipeline/                 # KÄRNAN: fetch → parse → compute → cache
│   ├── ipc/                  # IPC-kontrakt (tidigare application/models/ipc/)
│   │   ├── WorkRequest.h
│   │   ├── FetchResult.h
│   │   └── ParseResult.h
│   ├── fetch/                # (tidigare infrastructure/processes/fetcher/)
│   │   ├── Fetcher.c/h
│   │   └── main.c
│   ├── parse/                # (tidigare infrastructure/processes/parser/)
│   │   ├── Parser.c/h
│   │   └── main.c
│   ├── compute/              # (tidigare application/services/ + concurrency/threads/)
│   │   ├── Compute.c/h
│   │   └── ComputeWorker.c/h
│   └── cache/                # (tidigare infrastructure/cache/)
│       └── SharedCache.c/h
│
├── server/                   # HTTP-server (tidigare application/core/ + network/)
│   ├── Server.c/h
│   ├── ClientHandler.c/h
│   ├── HTTPRequest.c/h
│   ├── HTTPResponse.c/h
│   └── TCPServer.c/h
│
├── domain/                   # Ren affärslogik — noll OS-beroenden
│   ├── Energy.c/h            # (tidigare application/models/domain/)
│   ├── Forecast.h
│   ├── SpotPrice.h
│   ├── Weather.h
│   ├── UserConfig.h
│   ├── Scheduler.c/h         # (tidigare application/services/LoadScheduler)
│   └── Config.h
│
├── api/                      # Externa API-integrationer
│   ├── APIEndpoints.c/h      # URL-byggare
│   ├── APIParser.c/h         # JSON-parsare
│   ├── OpenMeteoResponse.h
│   └── ElprisetResponse.h
│
├── storage/                  # ALL databaslogik på ett ställe
│   ├── Database.c/h          # SQLite-wrapper
│   ├── UserConfigDB.c/h
│   └── ScheduleDB.c/h
│
├── net/                      # Nätverksklient (HTTPS mot externa API:er)
│   ├── HTTPClient.c/h
│   └── HTTPFetcher.c/h
│
├── auth/                     # JWT-validering (enheten validerar, utfärdar ej)
│   └── JWTValidator.c/h
│
├── system/                   # OS-primitiver (kursmål 1-3 demonstreras här)
│   ├── threads/
│   │   ├── ThreadPool.c/h
│   │   └── WorkerPool.c/h
│   ├── sync/
│   │   ├── Queue.c/h
│   │   ├── WorkCompletion.c/h
│   │   └── CompletionRegistry.c/h
│   ├── signals/
│   │   └── SignalHandler.c/h
│   ├── daemon/
│   │   ├── Daemon.c/h
│   │   └── PidFile.c/h
│   └── logging/
│       └── Logger.c/h
│
├── watchdog/                 # Watchdog-process
│   ├── Watchdog.c/h
│   └── main.c
│
├── client/                   # C++-klient (separat binary)
│   ├── main.cpp
│   ├── GridGuardClient.cpp/hpp
│   ├── HttpClient.cpp/hpp
│   ├── SocketGuard.hpp
│   └── UserConfigWrapper.cpp/hpp
│
├── vendor/                   # Tredjepartsbibliotek (oförändrade)
│   └── cJSON.c/h
│
└── tests/
    ├── unit/
    └── integration/
```

**Vad försvinner:**
- `src/database/` — slås ihop med `src/infrastructure/database/` → `src/storage/`
- `src/platform/` — tas bort helt (hör inte hit)
- `application/models/ipc/` → `pipeline/ipc/`
- `application/core/` → `server/`
- `application/configs/` → `domain/Config.h`
- `infrastructure/processes/*/` → `pipeline/*/` och `watchdog/`
- `network/` → `server/` (HTTP-protokoll) + `net/` (klient)

**Makefile reduceras till ~6 includes:**
```makefile
INCLUDES = -I$(SRC_DIR) -I$(SRC_DIR)/vendor \
           -I$(SRC_DIR)/pipeline/ipc \
           -I$(SRC_DIR)/domain \
           -I$(SRC_DIR)/system/sync \
           -I$(SRC_DIR)/system/logging
```

---

## 8. Prioriterad åtgärdslista

### Röd — måste åtgärdas (kursmål-kritiska)

1. **Profileringsrapport** — Kör `make profile`, kör servern under last (`scripts/benchmark.sh`), analysera med `gprof`, dokumentera. Identifiera compute-steget som trolig flaskhals. Skapa `docs/PROFILING_REPORT.md` med före/efter-mätningar. _(Kursmål 10, 11)_

2. **Bugg: `mktime()` → `timegm()`** i `src/infrastructure/processes/parser/parser.c:30`. Kritisk korrekthetsbug. _(Affärskritisk)_

3. **README.md** — Obligatoriskt leveranskrav. Minst: installation, beroenden, `make`, `make dev`, `make test`, API-endpoints, konfiguration. _(Leveranskrav)_

### Orange — bör åtgärdas (leverans- och strukturkvalitet)

4. **Flytta `platform/` ur `SERVER_SRCS_C`** — Lägg till `make platform-objects` som separat target. Ta bort raderna 154-155 ur `SERVER_SRCS_C`. Server-binären ska inte innehålla JWT-utfärdningskod — det förstärker den arkitektoniska poängen istället för att motbevisa den. `generate_jwt.py` fortsätter fungera utan ändringar. _(Arkitektur)_

5. **Slå ihop `src/database/` och `src/infrastructure/database/`** — Välj en plats (`src/storage/` rekommenderas), ta bort dubbletten. _(Struktur)_

6. **Fixa logisk bugg** — `else`-grenen i `parser.c:235-238`. Lägg till korrekt fallback (tom prislista → alla spotpriser = NaN/0 med varning till klienten). _(Korrekthet)_

7. **API-dokumentation** — Dokumentera alla REST-endpoints i `docs/API.md`: request/response-schema, auth-krav, felkoder. _(Leveranskrav)_

8. **`.gitignore`** — Lägg till `*.db`, `bin/`, `build/`, `logs/*.log`, `gmon.out`. _(Hygien)_

### Gul — förbättringar (om tid finns)

9. **Strukturrefaktorering** till Pipeline-First enligt avsnitt 7. Påbörja med att flytta `ipc/`-structs och ta bort `platform/`. _(Långsiktig kvalitet)_

10. **IPC-protokoll: lägg till magic/version** i `FetchResult` och `ParseResult` för att detektera version-mismatch vid runtime. _(Robusthet)_

11. **`sscanf`-returvärde** i `parser.c:26` — kontrollera att 6 fält parsades. _(Defensiv kod)_

12. **Arkitekturdokumentation** — Uppdatera `docs/` med ett enda, aktuellt dokument inkl. IPC-flödesdiagram och minnesmodell. _(Kursmål 12)_

---

## Slutbedömning

GridGuard är ett **tekniskt ambitiöst och väl genomtänkt projekt** som demonstrerar genuint systemnära C-programmering. IPC-kedjan med FIFO + Unix socket + shared memory är korrekt implementerad och visar god förståelse för kursmaterialet. Compute-motorn är på en professionell nivå med välmotiverade fysikaliska konstanter.

De tre återstående problemen är dock kritiska inför inlämningen:

1. **Kursmål 10-11 (profilering)** är helt odemonstrerade — detta är ett obligatoriskt examinerande moment.
2. **README saknas** — obligatoriskt leveranskrav.
3. **`mktime()`-buggen** ger systematiskt fel energiplan.

Mappstrukturen har blivit rörig men är inte ett examineringshinder i sig — det är dock ett tecken på att arkitekturen vuxit organiskt utan en genomtänkt plan. Rekommendationen är att prioritera profileringsrapporten och README:n före en full strukturrefaktorering.

---

*Analys utförd av Claude Code baserat på fullständig genomläsning av samtliga källkodsfiler, Makefile, Kursplanering.pdf och BoilerRoomProjekt.pdf.*
