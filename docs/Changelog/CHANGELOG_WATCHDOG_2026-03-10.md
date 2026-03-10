# Watchdog Process Supervision - Changelog

**Branch:** `feature/watchdog-process-supervision`
**Datum:** 2026-03-10
**Status:** Redo för testing och merge

---

## TL;DR - Vad har ändrats?

Vi har gått från ett system där Server spawnar och hanterar Fetcher och Parser själv, till ett där **Watchdog fungerar som en supervisor** som startar och övervakar alla tre processer. Detta ger oss automatisk crash recovery, freeze detection och en mycket tydligare ansvarsfördelning.

**Huvudvinster:**
- 🔄 Automatisk restart vid crash
- 💓 Heartbeat-baserad freeze-detection
- 🎯 Tydligare ansvarsfördelning (Watchdog owns IPC-resurser)
- 🚀 Eliminerar race conditions vid startup
- 🔧 Enklare debugging (varje process helt fristående)

---

## Problemet vi hade

I `development`-branchen såg arkitekturen ut såhär:

```
Server (main process)
├── Skapar FIFO och Unix socket
├── Skapar anonymous pipe
├── fork() → Fetcher
│   └── Läser från stdin (pipen)
├── fork() → Parser
└── pthread_create() → ComputeWorker
```

**Problem:**
1. **Ingen crash recovery** - Om Fetcher eller Parser kraschade upptäckte vi det inte förrän nästa request hängde
2. **Tight coupling** - Server ansvarade för att spawna children, hantera IPC-resurser OCH serva HTTP
3. **Race conditions** - Måste använda `sleep(1)` mellan fork-operationer för att säkerställa att FIFO-ändarna öppnades i rätt ordning
4. **Zombie-processer** - Om Server dog utan graceful shutdown blev children zombies
5. **Ingen freeze detection** - En process som hängde i deadlock eller infinite loop upptäcktes aldrig

---

## Lösningen: Watchdog som supervisor

Ny arkitektur i `feature/watchdog-process-supervision`:

```
Watchdog (supervisor)
├── Skapar ALLA IPC-resurser innan spawn
│   ├── /tmp/gridguard_requests.fifo (Server → Fetcher)
│   ├── /tmp/gridguard_fetch_to_parse.fifo (Fetcher → Parser)
│   └── /tmp/gridguard_parse_to_compute.sock (Parser → ComputeWorker)
│
├── Spawnar processer i rätt ordning
│   ├── Parser först (öppnar FIFO read end)
│   ├── Fetcher (öppnar FIFO write end, blockar inte)
│   └── Server (HTTP + ComputeWorker thread)
│
└── Monitrar alla tre via heartbeats
    ├── Parser skickar "heartbeat\n" var 5:e sekund
    ├── Fetcher skickar "heartbeat\n" var 5:e sekund
    └── Server skickar "heartbeat\n" var 5:e sekund
```

**Vad händer vid problem:**
- **Crash** (segfault, abort): Watchdog får SIGCHLD, loggar exit code, startar om alla tre processer
- **Freeze** (deadlock, infinite loop): Watchdog ser att heartbeat timeout (15s), dödar och startar om
- **Graceful exit**: Watchdog loggar, cleanup IPC-resurser, exit

---

## Nya moduler och funktioner

### 1. ProcessHeartbeat (`src/sys/ProcessHeartbeat.c/h`)

Enkel modul för att skicka heartbeats från child-processer tillbaka till Watchdog.

**Hur det fungerar:**

```c
// I Watchdog: Skapa pipe innan fork
Heartbeat *hb = Heartbeat_Create();

// I child-process: Läs FD från environment och skicka heartbeats
ProcessHeartbeat heartbeat;
ProcessHeartbeat_Initiate(&heartbeat, 5);  // 5 sekunders interval

while (running) {
    ProcessHeartbeat_Send(&heartbeat);  // Skickar "heartbeat\n" om 5s har passerat

    // ... gör arbete ...
}
```

**Watchdog-sidan:**
```c
// Non-blocking read från heartbeat pipe
char buf[32];
ssize_t n = read(hbFd, buf, sizeof(buf));
if (n > 0) {
    lastHeartbeat = time(NULL);  // Uppdatera timestamp
}

// Kolla timeout
if (difftime(time(NULL), lastHeartbeat) >= 15) {
    LOG_WARNING("Process frozen! Restarting all...");
    // Kill och restart
}
```

**Varför inte bara waitpid()?**
- waitpid() upptäcker bara crashes (exit/signal)
- Heartbeat upptäcker även freeze (process lever men gör inget)

---

### 2. Watchdog koordinerar IPC-skapande

**Före:**
- Server skapade FIFO för Fetch→Parse
- Server skapade Unix socket path (men Parser skapade socketen)
- Potential för race conditions (vem skapar först?)

**Efter:**
```c
// Watchdog skapar ALLA FIFOs innan någon process startas
static void CreateFifos(void) {
    mkfifo("/tmp/gridguard_requests.fifo", 0644);
    mkfifo("/tmp/gridguard_fetch_to_parse.fifo", 0644);
    unlink("/tmp/gridguard_parse_to_compute.sock");  // Rensa gammal socket
}

// Spawna i rätt ordning
CreateFifos();
parser_pid = Watchdog_SpawnParser(...);    // Öppnar FIFO read end först
sleep(1);  // Låt Parser komma igång
fetcher_pid = Watchdog_SpawnFetcher(...);  // Öppnar FIFO write end, blockar inte
sleep(1);
server_pid = Watchdog_SpawnServer(...);    // Sist, öppnar request FIFO
```

**Koordinerad cleanup:**
När Watchdog stänger ner (SIGTERM) eller när en process crashar:
```c
kill(fetcher_pid, SIGTERM);
kill(parser_pid, SIGTERM);
kill(server_pid, SIGTERM);
waitpid(fetcher_pid, ...);
waitpid(parser_pid, ...);
waitpid(server_pid, ...);
CleanupFifos();  // Rensa alla IPC-resurser
```

---

### 3. Fetcher: Från stdin till dedikerad FIFO

**Före:**
```c
// Server skapade anonymous pipe och redirect:ade stdin
pipe(requestPipe);
dup2(requestPipe[0], STDIN_FILENO);

// Fetcher läste från stdin
read(STDIN_FILENO, &request, sizeof(request));
```

**Problem:**
- Anonymous pipes är bra för parent→child, men Watchdog är nu parent
- Svårt att separera "vem äger vad"

**Efter:**
```c
// Fetcher öppnar dedikerad request FIFO
proc->requestFifoFd = open("/tmp/gridguard_requests.fifo", O_RDONLY);

// Använder select() med timeout för att inte blocka permanent
fd_set readfds;
FD_SET(proc->requestFifoFd, &readfds);
struct timeval timeout = {.tv_sec = 1};

int ready = select(proc->requestFifoFd + 1, &readfds, NULL, NULL, &timeout);
if (ready > 0) {
    read(proc->requestFifoFd, &request, sizeof(request));
}

// Skickar heartbeat varje loop (även vid timeout)
ProcessHeartbeat_Send(&heartbeat);
```

**Vinster:**
- Fetcher kan skicka heartbeats även när ingen request kommer
- Watchdog ser att Fetcher lever (inte blockerad i evighet)
- Tydligare dataflöde: Server → `/tmp/gridguard_requests.fifo` → Fetcher

---

### 4. Server: Förenklas dramatiskt

**Före:**
```c
// Server spawnade Fetcher och Parser
fetchPid = fork();
if (fetchPid == 0) { execl("bin/GridGuard-fetcher", ...); }

parsePid = fork();
if (parsePid == 0) { execl("bin/GridGuard-parser", ...); }

// Shutdown måste döda children
kill(fetchPid, SIGTERM);
kill(parsePid, SIGTERM);
waitpid(fetchPid, ...);
waitpid(parsePid, ...);
```

**Efter:**
```c
// Server öppnar bara request FIFO (Watchdog har redan skapat den)
app->requestPipeFd = open("/tmp/gridguard_requests.fifo", O_WRONLY);

// Shutdown är enklare - Watchdog äger andra processer
LOG_INFO("Server shutting down (Watchdog manages other processes)");
close(app->requestPipeFd);
```

**Vinster:**
- Server har ett enda ansvar: HTTP API + Compute
- Ingen fork/exec-logik i Server
- Lättare att testa (kan starta Server standalone för unit tests)

---

### 5. Forecast Cache tillagd

Ny cache för färdiga JSON-responses (kompletterar weather/price caches):

```c
SharedCache forecastCache;
SharedCache_Create(&app->forecastCache, "/gridguard_forecast", 900);  // 15 min TTL
```

**Cache-hierarki:**
```
forecastCache     ← Färdiga JSON-responses (END-TO-END cache)
    ↓
weatherCache      ← Rådata från SMHI API (används av Fetcher)
priceCache        ← Rådata från Sourceful API (används av Fetcher)
```

**Prestanda-vinst:**
- Cache MISS: ~1–2s (API-latency)
- Cache HIT: ~2–5ms (skip hela Fetcher→Parser→Compute pipeline)
- 400–1000× snabbare vid cache hit

---

## Filändringar i detalj

### Nya filer:
- ✨ `src/sys/ProcessHeartbeat.c` (55 rader) - Heartbeat-mekanism
- ✨ `src/sys/ProcessHeartbeat.h` (15 rader) - API

### Stora ändringar:
- 🔄 `src/watchdog/Watchdog.c` (+396 rader)
  - Spawnar tre processer istället för en "daemon"
  - Heartbeat-monitoring per process
  - FIFO-skapande och cleanup

- 🔄 `src/server/GridGuard.c` (-190 rader netto)
  - Tar bort all fork/exec-logik
  - Tar bort child process management
  - Öppnar FIFO istället för att skapa pipe

- 🔄 `src/fetcher/Fetcher.c` (+84 rader)
  - Läser från dedikerad FIFO istället för stdin
  - Använder select() för non-blocking med timeout
  - Skickar heartbeats varje sekund

### Små ändringar:
- `src/parser/Parser.c` - Heartbeat support
- `src/watchdog/main.c` - Uppdaterad för nya spawning-logiken
- `Makefile` - Kompilerar ProcessHeartbeat

---

## Hur man testar

### 1. Bygg och starta med Watchdog

```bash
make clean && make
bin/GridGuard-watchdog
```

**Förväntat i loggen:**
```
[INFO] Watchdog: Creating FIFOs...
[INFO] Parser started (PID 12345)
[INFO] Fetcher started (PID 12346)
[INFO] Server started (PID 12347)
[INFO] Watchdog: All processes started, entering monitor loop
```

### 2. Testa crash recovery

**Döda Fetcher-processen:**
```bash
# I ett annat terminal
pkill -9 GridGuard-fetcher
```

**Förväntat i Watchdog-loggen:**
```
[WARNING] Watchdog: Child process 12346 exited (signal 9)
[INFO] Watchdog: Restarting all processes...
[INFO] Parser started (PID 12348)
[INFO] Fetcher started (PID 12349)
[INFO] Server started (PID 12350)
```

### 3. Testa heartbeat monitoring

**Modifiera Fetcher temporärt för att stoppa heartbeats:**
```c
// I Fetcher.c, kommentera ut:
// ProcessHeartbeat_Send(&heartbeat);
```

**Bygga och starta:**
```bash
make && bin/GridGuard-watchdog
```

**Efter 15 sekunder:**
```
[WARNING] Watchdog: Fetcher heartbeat timeout (15s), process may be frozen
[INFO] Watchdog: Restarting all processes...
```

### 4. Verifiera IPC-resurser

**Visa FIFOs:**
```bash
ls -la /tmp/gridguard*.fifo /tmp/gridguard*.sock
```

**Förväntat:**
```
prw-r--r-- 1 user user 0 Mar 10 10:15 /tmp/gridguard_requests.fifo
prw-r--r-- 1 user user 0 Mar 10 10:15 /tmp/gridguard_fetch_to_parse.fifo
srwxr-xr-x 1 user user 0 Mar 10 10:15 /tmp/gridguard_parse_to_compute.sock
```

**Cleanup efter Watchdog stop:**
```bash
pkill GridGuard-watchdog
ls /tmp/gridguard*  # Ska vara tomt (Watchdog rensar upp)
```

---

## Status på tidigare kvarvarande arbete

### ✅ Klart (2026-03-10):

- [x] **Restart policy tuning**
  - Exponential backoff implementerat (2s → 4s → 8s → 16s → 32s)
  - Max 5 restarts per 5 minuter fungerar, window resettas automatiskt

- [x] **Signalhantering**
  - SIGUSR1 loggar process-status utan att störa
  - SIGUSR2 triggar manuell restart

- [x] **Metrics endpoint**
  - `/metrics` exponerar watchdog-status via shared memory
  - Visar uptime, restart-counter, heartbeat-timestamps

### Framtida förbättringar (Q2 2026):

- [ ] **Systemd integration**
  - `gridguard.service` unit file
  - Type=notify för proper startup signaling

- [ ] **Log rotation**
  - Watchdog roterar loggar automatiskt vid restart

- [ ] **Health checks**
  - Watchdog gör periodiska HTTP requests till `/health`
  - Om Server svarar men heartbeat saknas → partial failure detection

---

## Prestandapåverkan

**Overhead av heartbeats:**
- Varje process: 1× `write(fd, "heartbeat\n", 10)` per 5 sekunder
- ~2 μs per write
- **Total overhead: försumbar** (<0.001% CPU)

**Startup-tid:**
```
Före (development):  Server fork → Fetcher + Parser (~2s med sleep-delays)
Efter (watchdog):    Watchdog spawn → Parser → Fetcher → Server (~3s med sleep-delays)

Skillnad: +1s startup-tid (acceptabelt, händer bara vid systemstart)
```

**Runtime prestanda:**
- Oförändrad (samma IPC-mekanismer, bara annan process-hierarki)
- Forecast cache ger 400–1000× speedup vid cache hit (oberoende av watchdog)

---

## Migration från development

Om du utvecklar på en feature-branch baserad på `development`:

**Rebase mot `feature/watchdog-process-supervision`:**
```bash
git checkout din-feature-branch
git rebase feature/watchdog-process-supervision
```

**Eller merge:**
```bash
git merge feature/watchdog-process-supervision
```

**Lösning av konflikter:**
- Om du ändrat i `GridGuard.c`: Ta bort fork/exec-logik, använd FIFO istället för pipe
- Om du ändrat i `Fetcher.c`: Lägg till `ProcessHeartbeat_Send()` i main loop

---

## Sammanfattning för teamet

**Varför är detta viktigt?**
1. **Robusthet** - Systemet kan nu hantera crashes automatiskt utan manuell intervention
2. **Debuggability** - Varje process är helt fristående, enklare att testa och debugga
3. **Production-ready** - Heartbeat monitoring och automatic restart är standard i produktionssystem
4. **Separation of concerns** - Watchdog owns process lifecycle, Server owns HTTP, Fetcher owns API calls, Parser owns JSON

**Vad behöver du veta?**
- Starta alltid systemet via `bin/GridGuard-watchdog` (inte `bin/GridGuard-server` direkt)
- IPC-resurser skapas av Watchdog, inte av enskilda processer
- Vid debugging: Watchdog loggar till `logs/watchdog.log`

**Frågor?**
- Läs SYSTEM_OVERVIEW.md sektion 12 (Watchdog Process Supervision)
- Diskutera i team-möte eller skapa issue på GitHub

---

---

## Uppdateringar 2026-03-10 (slutförd)

### Restart policy med exponential backoff
Fick in ordentlig restart-logik i watchdog. Började med 2 sekunders delay, sen dubblar den varje gång: 2s → 4s → 8s → 16s → 32s. Max 5 restarts på 5 minuter, sen ger systemet upp och loggar att något är riktigt fel.

Counter:n resettas automatiskt när fönstret gått ut, så tillfälliga problem triggar inte shutdown. Rätt schysst faktiskt.

Koden finns i `src/watchdog/RestartPolicy.c`.

### Signaler för live-debugging (SIGUSR1/SIGUSR2)
Nu kan man köra `kill -USR1 <watchdog-pid>` och få process-status dumpat i loggen utan att störa något. Visar PID, senaste heartbeat och restart-counter.

`kill -USR2` triggrar manuell restart om man vill starta om allting direkt.

Tänkte det skulle vara nice att kunna inspektera systemet live i produktion. Följer kodstilen med `_Initiate()` som vi använder överallt.

Finns i `src/watchdog/WatchdogSignals.h/c`.

### Public metrics endpoint
La till `/metrics` som är öppen utan auth. Använder shared memory (`/gridguard_watchdog_metrics`) så server kan läsa watchdog-data utan att behöva göra IPC-calls.

Response innehåller:
- Watchdog uptime och restart-statistik
- PIDs för alla processer
- Senaste heartbeat-timestamps
- Hur länge sen varje process skickade heartbeat

Ganska användbart för monitoring-verktyg eller dashboards.

Server-sidan: `src/server/ClientHandler.c`
Watchdog-sidan: `src/watchdog/WatchdogMetrics.h/c`

### Kodkvalitet och dokumentation
Gick igenom alla watchdog-filer och la till kommentarer som faktiskt förklarar vad koden gör, inte bara upprepar funktionsnamnet. Fokuserade på *varför*, inte *vad*.

Fixade också namngivning:
- `WatchdogMetrics_Create()` → `WatchdogMetrics_Initiate()`
- `WatchdogMetrics_Destroy()` → `WatchdogMetrics_Shutdown()`

Följer samma pattern som resten av projektet nu.

### Makefile-förbättringar
Watchdog byggs automatiskt med `make`. La till några användbara targets:

```bash
make start       # Seedar DBs, startar allt, väntar på health check
make run-server  # Startar med watchdog (samma som start)
make server-run  # Alias för run-server
make stop        # Stoppar allt och rensar IPC-resurser
```

`make start` är nice för development - du får hela stacken igång på ett kommando.

### Buggfix: Segfault i watchdog
Watchdog kraschade vid startup. Problemet var att den öppnade shared memory i read-only mode (`WatchdogMetrics_Open()`), sen försökte skriva till det. Klassisk.

Lösning: La till `WatchdogMetrics_GetWritable()` som returnerar den interna writeable-pointern. `Open()` används nu bara av server för att läsa metrics.

### Nya filer
- `src/watchdog/WatchdogMetrics.h/c` - Shared memory för metrics
- `docs/DEMO_GUIDE.md` - Steg-för-steg guide för demo/presentation

### Modifierade filer
- `src/watchdog/Watchdog.c` - Metrics, signals, kommentarer
- `src/watchdog/WatchdogSignals.h/c` - SIGUSR1/SIGUSR2
- `src/server/ClientHandler.c` - /metrics endpoint
- `Makefile` - Watchdog build, nya targets

---

## Refaktorering och kod-cleanup (2026-03-10 kväll)

Watchdog-koden hade blivit lite tjock - `Watchdog.c` låg på ~600 rader och innehöll allt från process spawning till IPC-hantering till status-rapportering. Kändes som rätt tillfälle att dela upp det i naturliga moduler.

### Vad vi gjorde

Bröt ut watchdog i fem fokuserade moduler:

**IPC** (`src/watchdog/IPC.h/c`) - Skapar och städar upp FIFOs och socket paths. Tänkte att watchdog "äger" IPC-resurserna så det borde vara en egen grej.

**Status** (`src/watchdog/Status.h/c`) - Hanterar den non-blocking FIFO:n som streamar watchdog-events. Tidigare var det bara några statiska funktioner i Watchdog.c, nu är det en riktig modul.

**ProcessSpawner** (`src/watchdog/ProcessSpawner.h/c`) - Process-hantering med `ProcessGroup` struct. Istället för att hålla koll på tre olika PID:ar och tre heartbeats, har vi nu en grupperad datastruktur. Mycket renare när man ska döda/vänta på alla processer samtidigt.

**Signals** (`src/watchdog/Signals.h/c`) - Signal handlers för SIGUSR1/SIGUSR2. Fanns redan men döpte om från WatchdogSignals för konsistens.

**Metrics** (`src/watchdog/Metrics.h/c`) - Shared memory för metrics. Samma här, var WatchdogMetrics förut.

### Naming convention-fix

Gick igenom alla watchdog-moduler och fixade naming:
- `IPC_CreateFifos()` → `IPC_Initiate()`
- `IPC_CleanupFifos()` → `IPC_Shutdown()`
- `Status_Create()` → `Status_Initiate()`
- `Status_Destroy()` → `Status_Shutdown()`

Nu följer allt samma pattern som `Logger_Initiate()`/`Logger_Shutdown()` och resten av projektet.

### Kommentarer som faktiskt hjälper

Tog bort alla AI-genererade kommentarer typ "// Create and open status FIFO at specified path". Ersatte med faktiska förklaringar:

```c
// Non-blocking FIFO for streaming watchdog events to monitoring tools
```

Fokuserade på *varför* något finns, inte bara *vad* det gör. La till inline-kommentarer på defines där det behövdes:

```c
#define HEARTBEAT_INTERVAL  5   // Process sends heartbeat every 5s
#define HEARTBEAT_TIMEOUT   15  // Watchdog considers process frozen after 15s
```

Heartbeat.h hade svenska kommentarer (!), fixade det också.

### Varför göra detta nu?

Watchdog-koden ska vara lätt att förstå. Om någon annan (eller framtida-jag) behöver felsöka en restart-loop eller lägga till ny funktionalitet, ska det vara uppenbart var koden finns. En 600-raders fil med allt i gör det svårt. Fem moduler på ~100 rader var? Mycket enklare.

Plus att det kändes fel att ha `WatchdogIPC_CreateFifos()` när ingenting annat i projektet använder det mönstret. Nu är allt `Module_Initiate()`/`Module_Shutdown()`.

### Resultat

`Watchdog.c`: 600 rader → 342 rader

Modulerna:
- `IPC.c`: 38 rader
- `Status.c`: 90 rader
- `ProcessSpawner.c`: 180 rader
- `Signals.c`: 50 rader (oförändrat)
- `Metrics.c`: 120 rader (oförändrat)

Watchdog.c fokuserar nu på det den ska göra: monitoring loop och restart-logik. Allt annat är uppdelat.

### Main launcher

Skapade `src/main.c` som fungerar som systemets entry point. Istället för att användare behöver veta att de ska köra `bin/GridGuard-watchdog`, kör de bara `bin/GridGuard`.

Launcher:n är simpel - den gör bara `execv()` till watchdog och forward:ar alla argument. Kändes mer professionellt att ha ett tydligt entry point, speciellt om vi senare vill lägga till modes (start/stop/status/etc).

Nu:
```bash
bin/GridGuard              # Main entry point
bin/GridGuard-watchdog     # Supervised by main
bin/GridGuard-server       # Supervised by watchdog
bin/GridGuard-fetcher      # Supervised by watchdog
bin/GridGuard-parser       # Supervised by watchdog
bin/GridGuard-client       # Standalone CLI
```

Före hade vi ingen tydlig "huvudbinär" - man var tvungen att veta att watchdog är entry point:en.

---

## Hur watchdog fungerar idag

GridGuard-watchdog är en klassisk supervisor-process som startar och övervakar tre child-processer: Fetcher, Parser och Server.

**Startup-sekvens:**

1. Watchdog skapar alla IPC-resurser (FIFOs och socket paths)
2. Spawnar Parser först (öppnar FIFO read end)
3. Väntar 1 sekund
4. Spawnar Fetcher (öppnar FIFO write end, blockar inte eftersom Parser redan lyssnar)
5. Väntar 1 sekund
6. Spawnar Server sist

Varje process får en heartbeat-pipe via miljövariabeln `GRIDGUARD_HEARTBEAT_FD`. Processen skriver "heartbeat\n" var 5:e sekund för att visa att den lever.

**Monitoring:**

Watchdog kör en main loop som:
- Läser heartbeats från alla tre pipes (non-blocking)
- Kollar `waitpid(-1, WNOHANG)` för crashes
- Uppdaterar metrics i shared memory

Om en process crashar (segfault, abort, exit != 0), eller om heartbeat timeout:ar (15 sekunder), dödar watchdog alla tre processer och startar om dem.

**Restart-logik:**

Exponential backoff med rate limiting:
- Första restart: 2 sekunder delay
- Andra restart: 4 sekunder
- Tredje restart: 8 sekunder
- ...och så vidare upp till 32 sekunder

Max 5 restarts på 5 minuter. Om vi når limiten loggar watchdog FATAL och ger upp - något är fundamentalt trasigt och det hjälper inte att fortsätta restart-loopa.

Counter resettas automatiskt när 5-minutersfönstret gått ut, så tillfälliga problem (t.ex. databas-timeout som fixar sig själv) triggar inte permanent shutdown.

**Signaler:**

- `SIGTERM`/`SIGINT`: Graceful shutdown - dödar alla child-processer, rensar IPC-resurser
- `SIGUSR1`: Dumpar process-status i loggen (PID, uptime, heartbeat-timestamps)
- `SIGUSR2`: Tvingar omstart av alla processer direkt

**Metrics:**

Watchdog skriver kontinuerligt till shared memory (`/gridguard_watchdog_metrics`). Server läser detta och exponerar via `/metrics`-endpoint. Detta ger oss live-status utan att behöva göra IPC-calls mellan watchdog och server.

**Dataflöde:**

```
Watchdog
├── Skapar FIFOs och socket paths
├── Spawnar Parser, Fetcher, Server
└── Monitor loop (2s poll):
    ├── Läs heartbeats från pipes
    ├── Kolla waitpid() för crashes
    ├── Uppdatera metrics
    └── Om problem → Kill all → Restart all
```

Det är medvetet enkelt. Watchdog gör en sak: håller koll på att processer lever. Om något går fel, starta om. Inga komplicerade heuristiker eller partial restarts - vi rensar hela state:n och börjar om från början.

---

**Status:** Klart för merge till `development`. Alla features testade, koden är refaktorerad och dokumenterad.
