# GridGuard — Arkitektur och Systemdesign

Smart energioptimering för svenska hushåll. GridGuard hämtar spotpriser och väderdata i realtid, beräknar förväntad solcellsproduktion och genererar en löpande energiplan — köp billigt, sälj överskott, undvik toppar.

---

## 1. Process-hierarki

```
bin/GridGuard             (launcher — execv:ar watchdog direkt)
└── bin/GridGuard-watchdog (supervisor — äger alla processer)
    ├── bin/GridGuard-fetcher  (hämtar väder + spotprisdata)
    ├── bin/GridGuard-parser   (validerar och strukturerar rådata)
    └── bin/GridGuard-server   (HTTP API + energiberäkningar)
                └── [thread] ComputeWorker
```

**Launcher** (`src/main.c`) är en tunn wrapper som gör `execv` direkt på watchdog — den ersätter sig själv utan att lämna en extra process i trädet.

**Watchdog** är systemets rotprocess. Den:
- Skapar alla IPC-resurser (FIFOs, socket) innan processerna startas
- Forkar och exec:ar de tre child-processerna i rätt ordning
- Övervakar varje process via heartbeat-pipes
- Startar om hela gruppen vid krasj (med exponentiell backoff)
- Skriver processtatistik till POSIX shared memory (`/gridguard_watchdog_metrics`)
- Skriver PID-fil (`/var/run/gridguard.pid`)

**Server** äger inte längre sina child-processer — det gör Watchdog. Server initialiserar bara sin interna ComputeWorker-tråd och öppnar de IPC-resurser som Watchdog redan skapat.

---

## 2. Startsekvens

```mermaid
sequenceDiagram
    participant WD as Watchdog
    participant PA as Parser
    participant FE as Fetcher
    participant SV as Server

    WD->>WD: mkfifo(requests.fifo)
    WD->>WD: mkfifo(fetch_to_parse.fifo)
    WD->>WD: unlink(parse_to_compute.sock)
    WD->>WD: mkfifo(parse_to_compute.fifo)

    WD->>PA: fork() + execv(GridGuard-parser)
    Note over PA: Öppnar fetch_to_parse.fifo (read)
    WD->>WD: sleep(1) — väntar på att Parser öppnar FIFO

    WD->>FE: fork() + execv(GridGuard-fetcher)
    Note over FE: Öppnar fetch_to_parse.fifo (write)
    WD->>WD: sleep(1)

    WD->>SV: fork() + execv(GridGuard-server)
    Note over SV: Öppnar requests.fifo (write, blockerar tills Fetcher är redo)
    SV->>SV: Initialiserar SharedCache ×3
    SV->>SV: Startar ComputeWorker-tråd
    SV->>SV: Startar HTTP-server :8080
```

```
1. Watchdog startar
   ├── Laddar RuntimeConfig från config/gridguard.conf (optional, fallback till defaults)
   └── Skapar IPC-resurser:
       ├── mkfifo /tmp/gridguard_requests.fifo        (Server → Fetcher)
       ├── mkfifo /tmp/gridguard_fetch_to_parse.fifo  (Fetcher → Parser)
       ├── unlink /tmp/gridguard_parse_to_compute.sock
       └── mkfifo /tmp/gridguard_parse_to_compute.fifo (Parser → Server notify)

2. Parser startas (öppnar FIFO read-end innan Fetcher försöker öppna write-end)
   └── sleep(1)

3. Fetcher startas
   └── sleep(1)

4. Server startas
   ├── Öppnar /tmp/gridguard_requests.fifo (O_WRONLY — blockerar tills Fetcher är redo)
   ├── Initialiserar SharedCache ×3 (weather, price, forecast) med konfigurerbara TTL-värden
   ├── Startar ComputeWorker-tråd (Unix socket-klient mot Parser)
   └── Startar HTTP-server på konfigurerbar port (default: 8080)
```

---

## 2.1. Daemon-skapande (POSIX Double-Fork)

Server-processen transformeras till en bakgrundsdaemon enligt Unix best practices. När processen körs under Watchdog (detekteras via `GRIDGUARD_HEARTBEAT_FD` miljövariabel) skippas double-fork steget eftersom Watchdog redan ansvarar för processövervakning via `waitpid()`.

```mermaid
flowchart TB
    START[Daemon_Initiate kallas]
    CHECK{Under Watchdog?<br/>GRIDGUARD_HEARTBEAT_FD satt?}
    FORK1[Steg 1: fork och förälder avslutar]
    SETSID[Steg 2: setsid — skapa ny session]
    FORK2[Steg 3: fork igen och första barnet avslutar]
    CHDIR[Steg 4: chdir till /]
    CLOSE[Steg 5: stäng stdin/stdout/stderr]
    DEVNULL[Steg 6: omdirigera till /dev/null]
    SIGPIPE[Steg 7: signal SIGPIPE, SIG_IGN]
    DONE[Daemon-process redo]

    START --> CHECK
    CHECK -->|Nej - standalone| FORK1
    CHECK -->|Ja - watchdog-övervakad| CHDIR
    FORK1 --> SETSID
    SETSID --> FORK2
    FORK2 --> CHDIR
    CHDIR --> CLOSE
    CLOSE --> DEVNULL
    DEVNULL --> SIGPIPE
    SIGPIPE --> DONE
```

**Steg-för-steg:**

1. **Fork #1**: Förälder avslutar → barnet blir inte längre processgrupps-ledare
2. **setsid()**: Skapar ny session → lossnar från kontrollerande terminal
3. **Fork #2**: Första barnet avslutar → säkerställer att processen aldrig kan återfå en terminal (bara session leader kan öppna terminal)
4. **chdir("/")**: Byter till root-katalog → undviker att låsa någon mountpoint
5. **close(0, 1, 2)**: Stänger stdin, stdout, stderr
6. **dup2(/dev/null)**: Omdirigerar 0, 1, 2 till /dev/null → undviker krascher när kod skriver till stdout/stderr
7. **signal(SIGPIPE, SIG_IGN)**: Ignorerar SIGPIPE → klientdisconnects dödar inte daemon

**Watchdog-specialfall:**

När `GRIDGUARD_HEARTBEAT_FD` är satt (Watchdog körning):
- Steg 1-3 skippas (fork och setsid)
- Watchdog äger redan processträdet via `fork()` + `waitpid()`
- Om Server double-fork:ar skulle Watchdog förlora spårning av rätt PID
- Heartbeat-tråden skriver till `fd` som Watchdog läser för att detektera att processen lever

**Implementation:** `src/sys/Daemon.c:52-133`

---

## 3. IPC-kanaler

```mermaid
flowchart LR
    SV["Server\n(HTTP-tråd)"]
    FE["Fetcher\n(process)"]
    PA["Parser\n(process)"]
    CW["ComputeWorker\n(thread i Server)"]
    SC1[("SharedCache\nweather")]
    SC2[("SharedCache\nprice")]
    SC3[("SharedCache\nforecast")]
    WM[("SharedCache\nwatchdog_metrics")]
    WD["Watchdog\n(process)"]

    SV -->|"WorkRequest\nrequests.fifo (FIFO)"| FE
    FE -->|"FetchResult\nfetch_to_parse.fifo (FIFO)"| PA
    PA -->|"ParseResult\nparse_to_compute.sock (Unix socket)"| CW
    PA -->|"notify\nparse_to_compute.fifo (FIFO)"| CW
    CW -->|"energiplan JSON"| SC3
    SC3 -->|"cache lookup"| SV

    FE <-->|"läs/skriv"| SC1
    FE <-->|"läs/skriv"| SC2
    PA <-->|"läs"| SC1
    PA <-->|"läs"| SC2

    WD -->|"skriver metrics"| WM
    WM -->|"läser /metrics"| SV
```

```
Server ──[FIFO: gridguard_requests.fifo]──► Fetcher
                   WorkRequest (struct, binär)

Fetcher ──[FIFO: gridguard_fetch_to_parse.fifo]──► Parser
                   FetchResult (struct, binär)

Parser ──[Unix socket: gridguard_parse_to_compute.sock]──► ComputeWorker
                   ParseResult (struct, binär)

Parser ──[FIFO: gridguard_parse_to_compute.fifo]──► ComputeWorker
                   Notify-signal (trigger för socket-läsning)

Watchdog ──[POSIX shm: /gridguard_watchdog_metrics]──► Server (/metrics endpoint)
                   WatchdogMetrics (struct, read-only från server)

Server ──[POSIX shm: /gridguard_weather]──► Fetcher/Parser (SharedCache)
Server ──[POSIX shm: /gridguard_price]───► Fetcher/Parser (SharedCache)
Server ──[POSIX shm: /gridguard_forecast]─► (forecast-cache, intern)
```

**Varför FIFO och inte anonym pipe?**
Watchdog startar processerna som oberoende exec:ade binärer. Anonyma pipes ärvs bara vid fork+exec i samma process — Watchdog måste använda namngivna FIFOs för att processerna ska kunna hitta varandra utan att dela file descriptors via arv.

---

## 4. Heartbeat-mekanism

Varje child-process får en anonym pipe vid fork:en via miljövariabeln `GRIDGUARD_HEARTBEAT_FD`. Processen skriver periodiskt till denna pipe för att signalera att den lever.

```
Watchdog                          Child-process
   |                                   |
   |── pipe() ──────────────────────── |
   |   read_fd (watchdog behåller)     |
   |   write_fd (ärvsett av child) ────► setenv("GRIDGUARD_HEARTBEAT_FD", fd)
   |                                   |
   |── poll(read_fd, 2s) ◄──────────── write(write_fd, "hb", 2)
   |   timeout = ingen heartbeat       |
```

**Timeout-hantering:** Om en process inte skickat heartbeat på `HEARTBEAT_TIMEOUT` sekunder klassar Watchdog den som fryst (`FROZEN`) och dödar hela processgruppen innan omstart.

---

## 5. Restart Policy

| Parameter | Värde |
|---|---|
| Max omstarter | 5 |
| Tidsfönster | 300 sekunder |
| Backoff-strategi | Exponentiell: 2s → 4s → 8s → 16s → 32s (max) |

Vid krasj dödar Watchdog **alla** processer (inte bara den kraschade) och startar om hela gruppen. Anledning: processerna är sammankopplade via FIFOs — om Fetcher kraschar har Parser ingen write-end att läsa ifrån, vilket leder till ett hängt tillstånd ändå.

**Resetlogik:** Om inga krascher sker på 300 sekunder nollställs räknaren. En stabil körning kan alltså hålla 5 omstarter per 5-minutersfönster utan att systemet ger upp.

```c
// Exponentiell backoff (RestartPolicy.c)
int delay = base_backoff_sec;        // 2
for (int i = 0; i < count - 1; i++) // Dubblar för varje restart
    delay *= 2;                      // cap vid 32s
```

```mermaid
flowchart TD
    A([Process kraschar / fryst]) --> B{watchdogRunning?}
    B -- Nej --> Z([Shutdown — avsluta])

    B -- Ja --> C[Döda alla processer\nSIGTERM → sleep 1s]
    C --> D{RestartPolicy_CanRestart?}

    D -- Nej\nmax 5 krascher / 300s --> E([FATAL — Watchdog avslutar])

    D -- Ja --> F[RecordRestart\nBeräkna backoff-delay\n2s → 4s → 8s → 16s → 32s]
    F --> G[Vänta backoff\nkontrollerar watchdogRunning varje sekund]
    G --> H{watchdogRunning?}
    H -- Nej --> Z
    H -- Ja --> I[Rensa FIFOs\nSkapa nya FIFOs]
    I --> J[ProcessGroup_SpawnAll\nParser → Fetcher → Server]
    J --> K{Spawn lyckades?}
    K -- Nej --> E
    K -- Ja --> L[Återuppta heartbeat-övervakning]
    L --> A
```

---

## 6. Signalhantering

### Watchdog

| Signal | Effekt |
|---|---|
| `SIGTERM` / `SIGINT` | Graceful shutdown: vidarebefordrar SIGTERM till alla child-processer, väntar 3s, sedan SIGKILL |
| `SIGHUP` | Vidarebefordrar SIGHUP till alla child-processer (reload-signal) |
| `SIGUSR1` | Loggar processtatusrapport (PIDs, heartbeat-ålder, restart-räknare) |
| `SIGUSR2` | Manuell omstart av alla processer (triggar samma flöde som krasj-omstart) |

### Server

| Signal | Effekt |
|---|---|
| `SIGHUP` | Hot-reload av config från `config/gridguard.conf` (via `RuntimeConfig_Reload()`) |
| `SIGTERM` / `SIGINT` | Graceful shutdown via `SignalHandler` |
| `SIGPIPE` | Ignoreras (för att undvika crash vid client disconnect) |

```bash
kill -USR1 $(cat /var/run/gridguard.pid)   # Statusrapport i watchdog.log
kill -USR2 $(cat /var/run/gridguard.pid)   # Manuell omstart
kill -HUP $(cat /var/run/gridguard.pid)    # Reload config (alla processer)
```

---

## 7. Shared Memory — Cacher

Tre SharedCache-instanser används för att dela data mellan processer utan att gå via nätverk eller disk:

| Namn (shm) | Innehåll | Producent | Konsument | TTL (default) |
|---|---|---|---|---|
| `/gridguard_weather` | Väderdata (JSON, TTL-baserad) | Fetcher | Parser | 900s (15 min, konfigurerbar) |
| `/gridguard_price` | Spotprisdata (JSON, TTL-baserad) | Fetcher | Parser | 3600s (60 min, konfigurerbar) |
| `/gridguard_forecast` | Beräknad energiprognos (JSON) | ComputeWorker | Server (HTTP-svar) | 1800s (30 min, konfigurerbar) |
| `/gridguard_watchdog_metrics` | Processtatistik | Watchdog | Server (`/metrics`) | N/A |

**Synkronisering:** SharedCache använder `pthread_rwlock` lagrad i shared memory — flera readers kan läsa parallellt, writers får exklusivt lås. Watchdog metrics är read-only från serverns sida (inga lås krävs vid läsning av atomära värden).

**TTL:** Vädercachen och priscachen har en Time-To-Live som kan konfigureras via `config/gridguard.conf` (se sektion 14). Vid cache miss kör servern hela pipeline (Fetch → Parse → Compute) synkront och lagrar resultatet.

### Process-Shared RWLock

SharedCache-implementeringen använder `pthread_rwlock_t` med `PTHREAD_PROCESS_SHARED`-attribut för att möjliggöra synkronisering mellan processer:

```mermaid
flowchart TB
    SHM["/dev/shm/gridguard_weather<br/>(POSIX shared memory)"]

    SHM --> LOCK["pthread_rwlock_t<br/>(PTHREAD_PROCESS_SHARED)"]
    SHM --> DATA["CacheEntry[] data"]

    LOCK --> F["Fetcher Process<br/>pthread_rwlock_wrlock()"]
    LOCK --> P["Parser Process<br/>pthread_rwlock_rdlock()"]
    LOCK --> S["Server Process<br/>pthread_rwlock_rdlock()"]

    F -->|Write| DATA
    P -->|Read| DATA
    S -->|Read| DATA
```

**Viktiga egenskaper:**
- Låset lagras **inuti** shared memory-segmentet (ej i varje process)
- `PTHREAD_PROCESS_SHARED` gör låset synligt för alla processer med mmap:ad access
- Flera readers kan läsa parallellt, writer får exklusivt lås
- Kernel-baserad synkronisering via futex (snabbt)

**Layout i minnet:**
```
shm-segment (/dev/shm/gridguard_weather):
┌─────────────────────────────────────┐
│  pthread_rwlock_t  lock             │  (process-shared)
│  int               count            │
│  time_t            ttl              │
│  CacheEntry[16]    entries[]        │
│    char  key[64]                    │
│    char  data[32768]                │
│    time_t created_at                │
└─────────────────────────────────────┘
```

Implementering: `src/cache/SharedCache.c` (rad 45-60 för rwlock init)

---

## 8. Server-intern pipeline (per HTTP-request)

```
HTTP-request (/forecast)
    │
    ▼
ClientHandler::HandleForecast()
    │
    ├── SharedCache_Lookup() ──► HIT: returnera cachat JSON direkt
    │
    └── MISS:
        ├── WorkCompletion_Initiate()     (skapar condition variable)
        ├── RegisterCompletion(userId)    (registrerar i CompletionRegistry)
        ├── write(requestFifo, WorkRequest)  ──► Fetcher
        │
        │   [asynkron pipeline: Fetcher → Parser → ComputeWorker]
        │
        └── WorkCompletion_Wait()         (blockerar HTTP-tråden, timeout 30s)
            │
            ▼
        SharedCache_Store() + HTTP-svar
```

**Trådmodell:** Servern hanterar varje HTTP-klient i en separat tråd (ThreadPool). ComputeWorker är en dedikerad bakgrundstråd som lyssnar på Parser-notifieringar. `CompletionRegistry` är en mutex-skyddad map `userId → WorkCompletion*` som kopplar ihop HTTP-tråden med ComputeWorker-tråden.

---

## 9. ThreadPool-arkitektur

Servern använder en thread pool med N worker threads (default: 20) som hanterar inkommande HTTP-requests via ett producer-consumer-mönster.

```mermaid
flowchart TB
    TCP[TCP Accept Loop\nMain Thread] -->|ny client socket| Q[Work Queue\nmutex + cond var]

    Q -->|pthread_cond_wait| W1[Worker Thread 1]
    Q -->|pthread_cond_wait| W2[Worker Thread 2]
    Q -->|pthread_cond_wait| W3[Worker Thread 3]
    Q -.->|...| WN[Worker Thread N]

    W1 -->|HTTP Request| CH1[ClientHandler_Process]
    W2 -->|HTTP Request| CH2[ClientHandler_Process]
    W3 -->|HTTP Request| CH3[ClientHandler_Process]

    CH1 -->|Svar| C1[Client Socket 1]
    CH2 -->|Svar| C2[Client Socket 2]
    CH3 -->|Svar| C3[Client Socket 3]
```

**Synkronisering:**
- Main thread: `pthread_cond_signal()` när ny klient läggs till i kön
- Worker threads: `pthread_cond_wait()` blockerar tills arbete finns
- Queue skyddas av `pthread_mutex_t`

**Fördelar:**
- Begränsat antal trådar (undviker thread exhaustion)
- Automatisk load balancing mellan workers
- Låg latency för nya requests (trådar redan skapade)

Implementering: `src/sys/ThreadPool.c`, `src/sys/Queue.c`, `src/server/ClientHandler.c`

---

## 10. WorkCompletion — HTTP-tråd ↔ ComputeWorker synkronisering

När en HTTP-request kräver fresh data (cache miss) blockeras HTTP-tråden tills ComputeWorker färdigställt beräkningen. Detta löses med condition variables:

```mermaid
sequenceDiagram
    participant HTTP as HTTP Worker Thread
    participant REG as CompletionRegistry<br/>(mutex-skyddad map)
    participant FIFO as Request FIFO
    participant CW as ComputeWorker Thread

    HTTP->>HTTP: Cache miss
    HTTP->>REG: RegisterCompletion(userId)
    Note over REG: userId → WorkCompletion*

    HTTP->>FIFO: write(WorkRequest)

    HTTP->>HTTP: WorkCompletion_Wait()
    Note over HTTP: pthread_cond_wait()<br/>Thread blockeras här...

    Note over FIFO,CW: [Async: Fetcher → Parser → ComputeWorker]

    CW->>REG: FindCompletion(userId)
    CW->>HTTP: WorkCompletion_Signal()
    Note over HTTP: pthread_cond_signal()<br/>Thread väcks

    HTTP->>REG: UnregisterCompletion(userId)
    HTTP->>HTTP: Returnera cachat resultat
```

**Timeout-säkerhet:**
- `WorkCompletion_Wait()` använder `pthread_cond_timedwait()` med 30s timeout
- `CLOCK_MONOTONIC` används (säkert mot systemtidsändringar)
- Om timeout inträffar returneras HTTP 504 Gateway Timeout

**Minnessäkerhet:**
- `CompletionRegistry` är en global array med 1024 slots
- Mutex skyddar både registrering och signalering
- WorkCompletion allokeras på stacken i HTTP-tråden (ej heap)

Implementering: `src/sys/WorkCompletion.c`, `src/sys/CompletionRegistry.c`

---

## 11. C++ Client — RAII och STL

GridGuard-klienten är implementerad i C++ och demonstrerar RAII (Resource Acquisition Is Initialization), STL-användning och exception-säker kod.

```mermaid
classDiagram
    class SocketGuard {
        -int fd
        +SocketGuard(int socket)
        +~SocketGuard()
        +get() int
        +release() int
    }

    class HttpClient {
        -std::unique_ptr~char[]~ buffer
        -std::vector~std::string~ headers
        +HttpClient(host, port)
        +get(path) std::string
        +post(path, body) std::string
        -parseResponse() void
    }

    class GridGuardClient {
        -HttpClient client
        -std::string token
        +GridGuardClient(host, port, token)
        +getForecast() json
        +getUserConfig() json
        +setUserConfig(config) void
    }

    class UserConfigWrapper {
        +toC() UserConfig*
        +fromC(UserConfig*) void
    }

    SocketGuard --o HttpClient : används av
    HttpClient --o GridGuardClient : ägs av
    UserConfigWrapper ..> GridGuardClient : används av
```

**RAII-exempel (SocketGuard):**
```cpp
class SocketGuard {
    int fd;
public:
    explicit SocketGuard(int socket) : fd(socket) {}
    ~SocketGuard() { if (fd >= 0) close(fd); }  // Automatisk cleanup

    SocketGuard(const SocketGuard&) = delete;              // Ej kopierbar
    SocketGuard& operator=(const SocketGuard&) = delete;
};
```

**STL-användning:**
- `std::unique_ptr<char[]>` — smart pointer för nätverksbuffertar (Week 9)
- `std::vector<std::string>` — dynamiska arrayer för HTTP headers (Week 9)
- `std::map<std::string, std::string>` — JSON-parsing (Week 9)
- Range-based for loops för iteration (C++11)

**Exception safety:**
- HttpClient kastar exceptions vid nätverksfel
- RAII garanterar att sockets stängs även vid exception
- Strong exception guarantee: operation lyckas helt eller inte alls

Implementering: `src/client/HttpClient.cpp`, `src/client/GridGuardClient.cpp`, `src/client/UserConfigWrapper.cpp`

---

## 12. Designbeslut och avvägningar

### Varför separata processer istället för trådar för Fetcher/Parser?

- **Isolation:** En krasch i Fetcher (t.ex. vid nätverksfel eller mallformad JSON) påverkar inte Server-processen
- **Oberoende körbara:** Fetcher och Parser kan kompileras, testas och deployas separat
- **IPC som kontrakt:** FIFOs och sockets tvingar fram explicita dataformat (WorkRequest, FetchResult, ParseResult) — inga oavsiktliga delad state

**Nackdel:** Kommunikation via FIFOs/sockets är dyrare än inter-thread communication. För GridGuard med typisk last (<10 req/min) är detta försumbart.

### Varför FIFO (named pipe) och inte Unix socket för Fetcher → Parser?

Named pipe passar bättre för enkel, enkelriktad dataström av fixa structs. Unix socket valdes för Parser → ComputeWorker eftersom ComputeWorker behöver kunna skilja på separata meddelanden (datagram-semantik).

### Varför dödar Watchdog alla processer vid en enskild krasj?

FIFOs är enkelriktade och blockerar. Om Fetcher dör har Parser ingen writer — `read()` returnerar EOF och Parser hänger eller avslutar sig ändå. En atomär omstart av hela gruppen är enklare och mer förutsägbar än att hantera partiella tillstånd.

### Minnesmodell för SharedCache

Cachen allokeras i POSIX shared memory (`shm_open` + `mmap`). Mutex/rwlock lagras *inuti* det delade minnessegmentet med `PTHREAD_PROCESS_SHARED`-attribut, så att alla processer kan använda samma lock utan att gå via kernel-calls som semaforer.

```
shm-segment layout:
┌──────────────────────────────────────┐
│  pthread_rwlock_t  lock              │  (i shared memory, process-shared)
│  int               count             │
│  time_t            ttl               │
│  CacheEntry[16]    entries[]         │
│    char  key[64]                     │
│    char  data[32768]                 │
│    time_t created_at                 │
└──────────────────────────────────────┘
```

---

## 10. Loggning

Varje process loggar till sin egen fil:

| Process | Loggfil |
|---|---|
| Watchdog | `logs/watchdog.log` |
| Server | `logs/server.log` |
| Fetcher | `logs/fetcher.log` |
| Parser | `logs/parser.log` |

Loggnivåer: `DEBUG`, `INFO`, `WARNING`, `ERROR`, `FATAL`. Nivå sätts vid `Logger_Initiate()` per process.

---

## 11. Konfigurationssystem

GridGuard använder ett runtime konfigurationssystem med tre nivåer (fallback chain):

```
1. Runtime config file (config/gridguard.conf) — INI-format, valfri
2. Environment variables (t.ex. GRIDGUARD_DB_PATH) — överskrider config-fil
3. Compile-time defaults (src/domain/Config.h) — fallback om inget annat anges
```

### Startup & Reload

**Startup:**
```bash
bin/GridGuard-watchdog --config /path/to/custom.conf
```

Watchdog laddar config före fork/exec av child-processer och skriver sökvägen till `GRIDGUARD_CONFIG_PATH`. Varje barnprocess (Fetcher, Parser, Server) ärver miljövariabeln via `fork()` och anropar `RuntimeConfig_Load` direkt vid uppstart.

**Hot-reload (SIGHUP):**
```bash
kill -SIGHUP $(cat /tmp/gridguard.pid)
```

Server upptäcker SIGHUP i sin main loop och kallar `RuntimeConfig_Reload()`. Nya värden används för nästa operation (cache TTL, HTTP timeout, etc.). Ingen omstart krävs.

### Konfigurerbara värden

**[server]**
- `port` — TCP-lyssningsport (default: 8080)

**[database]**
- `db_path` — Sökväg till gridguard.db (default: auto-upplöst, env: `GRIDGUARD_DB_PATH`)

**[network]**
- `timeout` — HTTP-timeout i sekunder (default: 30)
- `max_retries` — Återförsök vid HTTP-fel (default: 3)

**[cache]**
- `weather_ttl` — Väder-cache TTL (default: 900s)
- `price_ttl` — Priscache TTL (default: 43200s)
- `forecast_ttl` — Prognos-cache TTL (default: 1800s)

JWT-hemligheten hanteras **enbart via miljövariabel** — lagras aldrig i config-filen:
```bash
export GRIDGUARD_JWT_SECRET="din-hemlighet"
```

### Thread Safety

Config-access är thread-safe via `pthread_rwlock`:
- Multiple readers can access config simultaneously
- Reload operations acquire write lock (blocks until all readers finish)
- Zero performance impact during normal operation

Se `docs/CONFIG_DESIGN.md` för fullständig designdokumentation.

---

## 12. Binärer och byggsystem

```
bin/
├── GridGuard          (launcher)
├── GridGuard-watchdog (supervisor)
├── GridGuard-server   (HTTP API)
├── GridGuard-fetcher  (datahämtning)
├── GridGuard-parser   (datavalidering)
└── GridGuard-client   (C++-klient, RAII + STL)
```

```bash
make clean && make all   # Fullständig rebuild (krävs efter header-ändringar)
make dev                 # Bygg + seed databaser + starta + generera token
make stop                # Graceful shutdown via SIGTERM
```

**OBS:** Makefilen använder inte `-MMD` dependency tracking. Kör alltid `make clean` efter ändringar i header-filer som påverkar struct-storlekar (annars riskeras heap corruption p.g.a. felaktig struct-layout i stale .o-filer).
