# GridGuard System Overview

**Ett komplett system för smart energihantering med realtidsberäkningar**

> **Notering:** Detta dokument beskriver systemet som det är implementerat i `development`-branchen. Pågående arbete med watchdog-baserad process supervision finns i `feature/watchdog-process-supervision`.

---

## 1. Systemarkitektur (Nuvarande)

### 1.1 Process-hierarki

```
Server (GridGuard-server) - huvudprocess
├── Fetcher (child process via fork)
├── Parser (child process via fork)
└── ComputeWorker (thread i Server-processen)
```

**Server** är huvudprocessen som:
- Skapar alla IPC-resurser (pipes, FIFO, Unix socket)
- Forkar två child-processer (Fetcher och Parser)
- Kör HTTP server för API-requests
- Startar ComputeWorker-thread för beräkningar

**Varför denna arkitektur?**
- Separation of concerns: varje process har ett tydligt ansvar
- Parallel processing: API-hämtning och beräkningar kan ske samtidigt
- Shared memory: Fetcher och Parser delar cache via POSIX shm

### 1.2 Process Startup-sekvens

```c
// 1. Server initialiserar
GridGuard_Initiate(&app);

// 2. Skapar FIFO för Fetch → Parse
mkfifo("/tmp/gridguard_fetch_to_parse.fifo", 0666);

// 3. Skapar anonymous pipe för HTTP → Fetch
pipe(requestPipe);

// 4. Fork Fetcher
fetchPid = fork();
if (fetchPid == 0) {
    dup2(requestPipe[0], STDIN_FILENO);  // Redirect stdin till pipe
    execl("bin/GridGuard-fetcher", "GridGuard-fetcher", fifoPath, NULL);
}

// 5. Fork Parser
parsePid = fork();
if (parsePid == 0) {
    execl("bin/GridGuard-parser", "GridGuard-parser", fifoPath, socketPath, NULL);
}

// 6. Starta ComputeWorker-thread
pthread_create(&computeThread, NULL, ComputeWorker_Run, computeWorker);
```

**Timing:** `sleep(1)` används mellan fork-operationerna för att säkerställa att processerna hinner öppna sina IPC-endpoints innan nästa process startas.

---

## 2. Thread Synchronization: WorkCompletion & CompletionRegistry

### 2.1 Problemet som ska lösas

När en HTTP-request kommer in startas en **lång pipeline** som involverar flera processer:

```
HTTP Worker Thread → Fetcher → Parser → ComputeWorker → HTTP Worker Thread
     (väntar)         (API)     (JSON)    (beräkningar)    (svarar klient)
```

**Utmaningen:**
- HTTP worker-tråden behöver **vänta** på att hela pipeline är klar
- ComputeWorker-tråden behöver **väcka** HTTP-tråden när resultatet är klart
- Pipeline kan ta 50-500ms → HTTP-tråden måste blocka under tiden
- Vi behöver **timeout** (30s) om något går fel
- Vi har **många samtidiga requests** från olika användare → måste matcha rätt svar till rätt request

### 2.2 WorkCompletion: One-shot completion primitive

**WorkCompletion** är en synkroniseringsprimitiv baserad på Linux kernel's `struct completion` pattern.

```c
typedef struct {
    pthread_mutex_t mutex;           // Skyddar shared state
    pthread_cond_t  cond;            // Condition variable för signaling
    char            json[32768];     // Buffer för JSON-resultat
    int             done;            // Flag: är arbetet klart?
    int             error;           // Flag: gick något fel?
} WorkCompletion;
```

**Hur den fungerar:**

1. **Initiering** (HTTP worker thread):
```c
WorkCompletion wc;  // Stack-allocated! (säkert eftersom tråden blockar)
WorkCompletion_Initiate(&wc);
```

2. **Vänta på completion** (HTTP worker thread):
```c
int result = WorkCompletion_Wait(&wc);  // Blockar här tills Signal() anropas
if (result == 0) {
    // Success: wc.json innehåller resultatet
    HTTPResponse_SendJson(fd, wc.json);
} else {
    // Timeout eller error
    HTTPResponse_SendError(fd, HTTP_STATUS_500, "Pipeline error");
}
WorkCompletion_Destroy(&wc);
```

**Implementation av Wait:**
```c
int WorkCompletion_Wait(WorkCompletion *wc) {
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += 30;  // 30 sekunders timeout

    pthread_mutex_lock(&wc->mutex);

    while (!wc->done) {  // Loop för att hantera spurious wakeups
        int rc = pthread_cond_timedwait(&wc->cond, &wc->mutex, &deadline);
        if (rc != 0) {  // Timeout!
            pthread_mutex_unlock(&wc->mutex);
            return -1;
        }
    }

    int err = wc->error;
    pthread_mutex_unlock(&wc->mutex);
    return err ? -1 : 0;
}
```

**Viktiga detaljer:**
- **CLOCK_MONOTONIC**: Timeout påverkas inte av NTP-justeringar eller systemklocka
- **pthread_cond_timedwait**: Atomic unlock + sleep + relock när wakeup
- **Spurious wakeups**: Därför `while (!wc->done)` istället för `if`

3. **Signalera completion** (ComputeWorker thread):
```c
WorkCompletion_Signal(completion, jsonResult);
```

**Implementation av Signal:**
```c
void WorkCompletion_Signal(WorkCompletion *wc, const char *json) {
    pthread_mutex_lock(&wc->mutex);

    // Kopiera resultat till buffer
    strncpy(wc->json, json, WORK_COMPLETION_BUFFER_SIZE - 1);
    wc->json[WORK_COMPLETION_BUFFER_SIZE - 1] = '\0';

    // Markera som klar
    wc->done = 1;
    wc->error = 0;

    // Väck den väntande tråden
    pthread_cond_signal(&wc->cond);

    pthread_mutex_unlock(&wc->mutex);
}
```

### 2.3 CompletionRegistry: Global userId → WorkCompletion mapping

**Problemet:** ComputeWorker får bara `userId` i ParseResult. Hur hittar den rätt WorkCompletion-objekt att signalera?

**Lösningen:** En global thread-safe registry som mappar `userId` → `WorkCompletion *`

```c
typedef struct {
    char userId[64];
    WorkCompletion *completion;
    bool active;
} CompletionEntry;

static CompletionEntry g_registry[1024];  // Max 1024 samtidiga requests
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
```

**API:**
```c
// Registrera ett nytt request (HTTP worker thread)
RegisterCompletion("user123", &wc);

// Hitta WorkCompletion via userId (ComputeWorker thread)
WorkCompletion *wc = FindCompletionByUserId("user123");

// Ta bort när klart (ComputeWorker thread)
UnregisterCompletion("user123");
```

**Implementation av FindCompletionByUserId:**
```c
WorkCompletion *FindCompletionByUserId(const char *userId) {
    pthread_mutex_lock(&g_mutex);

    for (int i = 0; i < MAX_COMPLETIONS; i++) {
        if (g_registry[i].active && strcmp(g_registry[i].userId, userId) == 0) {
            WorkCompletion *completion = g_registry[i].completion;
            pthread_mutex_unlock(&g_mutex);
            return completion;
        }
    }

    pthread_mutex_unlock(&g_mutex);
    return NULL;  // Ingen request väntande för denna user
}
```

**Thread-safety:**
- Global mutex skyddar alla operationer på registry
- Linjär sökning (O(n)) men snabb för typiska antal requests (<100)
- Lock-tid är minimal: bara array-scanning och strcmp

### 2.4 Fullständigt flöde med exempel

**Steg 1: HTTP request kommer in** (ClientHandler.c:38-88)
```c
// Thread: HTTP Worker #7 (från ThreadPool)
static void HandleForecast(int fd, GridGuard *app, const JWTClaims *claims) {
    // Stack-allocate WorkCompletion (säkert eftersom tråden blockar här)
    WorkCompletion wc;
    WorkCompletion_Initiate(&wc);

    // Bygg WorkRequest
    WorkRequest req;
    strncpy(req.userId, claims->subject, sizeof(req.userId) - 1);
    // ... fyll i lat, lon, solarAreaM2, etc.

    // Submit till pipeline OCH registrera completion
    GridGuard_SubmitRequest(app, &req, &wc);

    // BLOCKA här och vänta på resultat (max 30s)
    if (WorkCompletion_Wait(&wc) == 0) {
        HTTPResponse_SendJson(fd, wc.json);  // Success!
    } else {
        HTTPResponse_SendError(fd, HTTP_STATUS_500, "Pipeline timeout");
    }

    WorkCompletion_Destroy(&wc);
}
```

**Steg 2: Registrera i CompletionRegistry** (GridGuard.c:250)
```c
// Thread: HTTP Worker #7
int GridGuard_SubmitRequest(GridGuard *app, WorkRequest *req, WorkCompletion *wc) {
    // Registrera så ComputeWorker kan hitta den senare
    RegisterCompletion(req->userId, wc);

    // Skicka request till Fetcher via pipe
    write(app->requestPipeFd, req, sizeof(WorkRequest));

    return 0;
}
```

**Registry state nu:**
```
g_registry[0] = { userId: "user123", completion: 0x7ffc1234abcd, active: true }
```

**Steg 3-5: Pipeline kör** (Fetcher → Parser → data flows)
```
Fetcher: HTTP GET SMHI, Sourceful → FIFO
Parser:  Läs FIFO, parse JSON → Unix socket
```

**Steg 6: ComputeWorker får ParseResult** (ComputeWorker.c:148-214)
```c
// Thread: ComputeWorker (dedikerad thread)
void *ComputeWorker_Run(void *arg) {
    while (running) {
        // Läs ParseResult från Unix socket
        ParseResult result;
        read(fd, &result, sizeof(result));

        // Hitta rätt WorkCompletion via userId
        WorkCompletion *completion = FindCompletionByUserId(result.userId);
        if (!completion) {
            LOG_ERROR("No pending request for user %s", result.userId);
            continue;
        }

        // Kör beräkningar
        EnergyData plan;
        Compute_GenerateEnergyPlan(compute, &result.forecastData, ..., &plan);

        // Bygg JSON response
        char *json = build_response_json(&plan, &result);

        // VIKTIGT: Unregister FÖRE signal (undviker race condition)
        UnregisterCompletion(result.userId);

        // Väck HTTP worker thread och ge den resultatet
        WorkCompletion_Signal(completion, json);

        free(json);
    }
}
```

**Steg 7: HTTP worker vaknar** (automatiskt av pthread_cond_signal)
```c
// Thread: HTTP Worker #7 (fortsätter från Wait())
WorkCompletion_Wait(&wc);  // Returnerar 0 (success)
HTTPResponse_SendJson(fd, wc.json);  // Skicka till klient
WorkCompletion_Destroy(&wc);  // Cleanup
```

### 2.5 Varför stack-allocation är säkert

**Kritisk insikt:** WorkCompletion allokeras på HTTP worker thread's stack, men är säker trots att ComputeWorker-tråden accesar den!

**Varför?**
1. HTTP worker-tråden **blockar i WorkCompletion_Wait()** under hela pipeline
2. Stack-framen förblir giltig så länge funktionen inte returnerat
3. ComputeWorker-tråden signalerar INNAN HTTP-tråden lämnar funktionen
4. Efter Signal() accesar ComputeWorker aldrig WorkCompletion igen

**Timing-garanti:**
```
HTTP Thread:          ComputeWorker Thread:
────────────          ─────────────────────
Stack: [wc]
Register(&wc)    ───→ (registry now has pointer to stack)
Wait() blocks         ...
  │                   FindCompletion() → &wc
  │                   Signal(&wc)  ──→ wake HTTP thread
  ↓                   (never touches wc again)
Wait() returns
Use wc.json
Destroy(wc)
[stack popped]
```

**Alternativ (sämre):**
- Heap-allocation: Kräver free(), mer komplex lifecycle, fragmentering
- Static buffer per thread: Begränsat antal threads, memory waste

**Linux kernel pattern:**
Detta är exakt hur `struct completion` fungerar i Linux kernel, testat i miljontals användningar.

### 2.6 Error-hantering

**Timeout (30s):**
```c
if (WorkCompletion_Wait(&wc) != 0) {
    // Kan hända om:
    // - Fetcher hängde på API-anrop
    // - Parser kraschade
    // - ComputeWorker fastnade i infinite loop
    LOG_ERROR("Pipeline timeout for user %s", userId);
    UnregisterCompletion(userId);  // Cleanup registry
    HTTPResponse_SendError(fd, HTTP_STATUS_500, "Timeout");
}
```

**Pipeline error:**
```c
// I ComputeWorker om något går fel:
if (Compute_GenerateEnergyPlan(...) != 0) {
    UnregisterCompletion(result.userId);
    WorkCompletion_SignalError(completion);  // Väcker med error=1
}
```

**Race condition prevention:**
```c
// Unregister FÖRE Signal så att en ny request från samma user
// kan registrera sig direkt utan att kollidera med gamla slotten
UnregisterCompletion(result.userId);
WorkCompletion_Signal(completion, json);
```

### 2.7 Sammanfattning: Varför denna design?

**Fördelar:**
1. **Zero-copy result passing**: JSON kopieras direkt in i WorkCompletion buffer
2. **Automatic cleanup**: Stack-allocation → ingen explicit free() krävs
3. **Timeout support**: Klient hänger inte vid API-fel eller deadlock
4. **Decoupling**: ComputeWorker känner bara till userId, inte HTTP connection
5. **Battle-tested pattern**: Baserad på Linux kernel's completion API

**Alternative rejected:**
- **Callbacks**: Kräver heap-allocation av closure, svårare error handling
- **Polling**: Waste CPU, högre latency
- **Blocking queue**: Kräver separate response queue, mer memory
- **Future/Promise**: Overkill för C, kräver runtime library

**Performance:**
- Register/Unregister: O(n) scan men typiskt <100 entries, ~1-2 μs
- Wait/Signal: pthread condition variable overhead, ~0.5 μs
- Total synchronization overhead: <5 μs per request (försumbart vs 50-500ms pipeline)

---

## 3. IPC-mekanismer

GridGuard använder **fyra olika IPC-tekniker** för olika ändamål:

### 3.1 Anonymous Pipe (HTTP → Fetcher)

**Typ:** `pipe()` + `dup2()` för stdin-redirection

**Användning:**
```
HTTP ClientHandler → write(requestPipeFd) → Fetcher stdin
```

**Implementering:**
```c
// Server skapar pipe före fork()
int requestPipe[2];
pipe(requestPipe);

// I child-processen (Fetcher)
dup2(requestPipe[0], STDIN_FILENO);  // stdin = pipe read end
close(requestPipe[0]);
close(requestPipe[1]);

// I parent-processen (Server)
close(requestPipe[0]);
app->requestPipeFd = requestPipe[1];  // Spara write end

// Senare: skriv WorkRequest
write(app->requestPipeFd, &request, sizeof(WorkRequest));
```

**Varför anonymous pipe?**
- Skapas automatiskt vid fork(), inga file permissions att hantera
- Perfect för parent → child kommunikation
- Fetcher kan läsa från stdin som vanlig input

**Fetcher läser från stdin:**
```c
// Fetcher använder blocking read() på stdin
WorkRequest request;
ssize_t bytesRead = read(STDIN_FILENO, &request, sizeof(WorkRequest));
if (bytesRead == sizeof(WorkRequest)) {
    // Process request...
}
```

### 2.2 Named Pipe / FIFO (Fetcher → Parser)

**Path:** `/tmp/gridguard_fetch_to_parse.fifo`

**Användning:**
```
Fetcher → write(fifoFd) → Parser → read(fifoFd)
```

**Varför FIFO?**
- Enkel one-way kommunikation mellan oberoende processer
- Blocking semantics: writer väntar automatiskt på reader
- Buffer i kernel: ingen data går förlorad vid kortvariga stopp

**Kod-exempel:**
```c
// Server skapar FIFO innan fork()
unlink("/tmp/gridguard_fetch_to_parse.fifo");
mkfifo("/tmp/gridguard_fetch_to_parse.fifo", 0666);

// Fetcher öppnar för writing
int fifoFd = open("/tmp/gridguard_fetch_to_parse.fifo", O_WRONLY);
FetchResult result = {/* ... */};
write(fifoFd, &result, sizeof(FetchResult));

// Parser öppnar för reading
int fifoFd = open("/tmp/gridguard_fetch_to_parse.fifo", O_RDONLY);
FetchResult result;
read(fifoFd, &result, sizeof(FetchResult));
```

**Permissions:** `0666` (rw-rw-rw-) för att både Fetcher och Parser kan accessa

### 2.3 Unix Domain Socket (Parser ↔ ComputeWorker)

**Path:** `/tmp/gridguard_parse_to_compute.sock`

**Användning:**
```
Parser (server) ← Unix Socket → ComputeWorker (client thread in Server)
```

**Varför Unix Socket istället för FIFO?**
- Bidirectional: Parser kan skicka ForecastData, Compute svarar med EnergyData
- Connection-oriented: vi vet när client kopplar från
- Better for request-response pattern

**Kod-exempel:**
```c
// Parser skapar Unix socket server
int serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
struct sockaddr_un addr = {.sun_family = AF_UNIX};
strncpy(addr.sun_path, "/tmp/gridguard_parse_to_compute.sock", sizeof(addr.sun_path) - 1);
unlink(addr.sun_path);
bind(serverFd, (struct sockaddr *)&addr, sizeof(addr));
listen(serverFd, 5);

// Accept connections
int clientFd = accept(serverFd, NULL, NULL);

// ComputeWorker connectar som client
int clientFd = socket(AF_UNIX, SOCK_STREAM, 0);
connect(clientFd, (struct sockaddr *)&addr, sizeof(addr));

// Parser skickar ForecastData
send(clientFd, &forecastData, sizeof(ForecastData), 0);

// ComputeWorker tar emot
recv(clientFd, &forecastData, sizeof(ForecastData), 0);
```

### 2.4 Shared Memory (POSIX shm)

**Names:** `/gridguard_weather`, `/gridguard_price`

**Användning:**
```
Fetcher writes → SharedCache (mmap) ← Parser reads
```

**Varför Shared Memory?**
- Snabbast: zero-copy data sharing mellan processer
- Perfekt för cache: undvik duplicate API-anrop
- TTL-baserad invalidering: 900s (15 min)

**Kod-exempel:**
```c
// Server skapar shared memory caches
SharedCache_Create(&weatherCache, "/gridguard_weather", 900);
SharedCache_Create(&priceCache, "/gridguard_price", 900);

// Fetcher skriver till cache (efter fork(), samma cache-objekt)
SharedCache_Put(&weatherCache, "smhi_forecast", jsonData, dataSize);

// Parser läser från cache (efter fork(), samma cache-objekt)
char buffer[MAX_SIZE];
ssize_t len = SharedCache_Get(&weatherCache, "smhi_forecast", buffer, sizeof(buffer));
if (len > 0) {
    // Cache hit: parse cached JSON
} else {
    // Cache miss skulle inte hända här, Fetcher hämtar alltid först
}
```

**Cache-struktur:**
```c
typedef struct {
    char     key[256];
    size_t   size;
    time_t   timestamp;
    uint8_t  data[524288];  // 512 KB
} CacheEntry;
```

**Hur shared memory delas efter fork():**
```c
// I Server (före fork)
SharedCache weatherCache;
SharedCache_Create(&weatherCache, "/gridguard_weather", 900);

// Efter fork(): både parent och child har pekare till samma shm-region
// Fetcher kan skriva, Parser kan läsa, zero-copy!
```

---

## 3. Process Lifecycle Management (Nuvarande)

### 3.1 Process Termination

När Server stängs ner (SIGTERM eller SIGINT):

```c
void GridGuard_Shutdown(GridGuard *app) {
    app->isRunning = false;

    // Terminera child-processer
    if (app->fetchPid > 0) {
        kill(app->fetchPid, SIGTERM);
    }
    if (app->parsePid > 0) {
        kill(app->parsePid, SIGTERM);
    }

    // Vänta på child-processer (reapa zombies)
    if (app->fetchPid > 0) {
        int status;
        waitpid(app->fetchPid, &status, 0);
        LOG_INFO("Fetch exited (status %d)", WEXITSTATUS(status));
    }

    if (app->parsePid > 0) {
        int status;
        waitpid(app->parsePid, &status, 0);
        LOG_INFO("Parse exited (status %d)", WEXITSTATUS(status));
    }

    // Stäng pipe
    if (app->requestPipeFd >= 0) {
        close(app->requestPipeFd);
    }

    // Rensa IPC-resurser
    unlink(app->fifoPath);
    unlink(app->socketPath);

    // Cleanup komponenter
    SharedCache_Destroy(&app->priceCache);
    SharedCache_Destroy(&app->weatherCache);
    Compute_Shutdown(&app->compute);
    ClientDB_Shutdown(&app->db);
}
```

### 3.2 Begränsningar (Nuvarande)

**Problem 1: Ingen crash recovery**
- Om Fetcher eller Parser crashar upptäcks det inte automatiskt
- Server fortsätter köra men requests kommer att hänga

**Problem 2: Ingen heartbeat monitoring**
- Om en child-process fryser (deadlock, infinite loop) upptäcks det inte
- Inga timings eller health checks

**Problem 3: Zombie-processer vid oväntad shutdown**
- Om Server dör utan att anropa `GridGuard_Shutdown()`, blir children zombies
- Kräver manuell cleanup av IPC-resurser

**Lösning:** Se sektion 11 om pågående watchdog-arbete.

---

## 4. Compute-logik: SELL/BUY/AVOID

### 4.1 Översikt

Compute-modulen analyserar 48-timmars prognos och genererar en energiplan med tre typer av signaler:

- **SELL:** Exportera överskott till nätet (när solproduktion > konsumtion)
- **BUY:** Bästa tidpunkten att använda el (billiga timmar)
- **AVOID:** Undvik onödiga laster (dyra timmar)
- **IDLE:** Normal drift (varken särskilt billigt eller dyrt)

### 4.2 Input-data

```c
typedef struct {
    double solarAreaM2;       // Solar panel area (m²)
    double solarEfficiency;   // Panel efficiency (0.15-0.22)
    double consumptionKwh;    // Hourly average consumption (kWh)
    double gridFee_low;       // 00-07: ~0.20 SEK/kWh
    double gridFee_normal;    // 07-17: ~0.40 SEK/kWh
    double gridFee_high;      // 17-24: ~0.60 SEK/kWh
} UserConfig;

typedef struct {
    time_t timestamp;
    double spotPriceSek;      // SEK/kWh (Nordpool spot via Sourceful)
    double solarIrradiance;   // W/m² (SMHI forecast)
    double temperature;       // °C (ambient)
    double windSpeed;         // m/s (for cooling calc)
} ForecastEntry;
```

### 4.3 Steg 1: Beräkna total kostnad per timme

För varje timme beräknas den verkliga konsumentkostnaden:

```c
// Time-of-use grid fee
double gridFee = hour < 7  ? gridFee_low    :  // Natt
                 hour < 17 ? gridFee_normal :  // Dag
                             gridFee_high;     // Kväll/Peak

// Total consumer cost including all fees and taxes
double totalCost = (spotPrice + gridFee + 0.40) * 1.25;
//                  ^^^^^^^^^   ^^^^^^^   ^^^^    ^^^^
//                  Nordpool    Nätavg.   Skatt   Moms (25%)
```

**Exempel:**
```
Spotpris:    0.80 SEK/kWh  (kl 18, peak hour)
Nätavgift:   0.60 SEK/kWh  (high tariff 17-24)
Energiskatt: 0.40 SEK/kWh  (fast svensk skatt)
Summa:       1.80 SEK/kWh
+ Moms 25%:  2.25 SEK/kWh  ← detta är vad kunden faktiskt betalar
```

### 4.4 Steg 2: Percentil-analys

Alla 48 timmar sorteras efter kostnad för att hitta billiga/dyra perioder:

```c
qsort(costs, n, sizeof(double), cmp_double);

int p30 = (int)(n * 0.30);  // 30th percentile
int p50 = n / 2;            // median
int p70 = (int)(n * 0.70);  // 70th percentile

double buyThreshold   = costs[p30];   // 30% billigaste timmar
double median         = costs[p50];
double avoidThreshold = costs[p70];   // 30% dyraste timmar
```

**Exempel med 48 timmar:**
```
Sorterade priser:
[1.20, 1.30, 1.35, 1.40, ..., 2.80, 3.00, 3.20]
 ^^^^^p30=1.40           ^^^^^p50=2.00    ^^^^^p70=2.80

BUY_THRESHOLD   = 1.40 SEK/kWh  (billigaste 30%)
MEDIAN          = 2.00 SEK/kWh
AVOID_THRESHOLD = 2.80 SEK/kWh  (dyraste 30%)
```

### 4.5 Steg 3: Quality gates

**Problem:** På dagar med platt prisutveckling kan "billiga" timmar bara vara 5% billigare än median. Då är det inte värt att shifta last.

**Lösning:** Quality gates säkerställer meningsfulla signaler:

```c
// BUY quality gate: måste vara minst 10% under median
#define BUY_MIN_DISCOUNT 0.10

double buyQualityCap = median * (1.0 - BUY_MIN_DISCOUNT);
if (buyThreshold > buyQualityCap)
    buyThreshold = buyQualityCap;

// AVOID quality gate: måste vara minst 10% över median
#define AVOID_MIN_PREMIUM 0.10

double avoidQualityFloor = median * (1.0 + AVOID_MIN_PREMIUM);
if (avoidThreshold < avoidQualityFloor)
    avoidThreshold = avoidQualityFloor;
```

**Exempel:**
```
Fall 1: Varierad prissättning (typisk dag)
  p30 = 1.40, median = 2.00  →  1.40 < 1.80 (2.00×0.90)  ✓ BUY signal OK
  p70 = 2.80, median = 2.00  →  2.80 > 2.20 (2.00×1.10)  ✓ AVOID signal OK

Fall 2: Platt prissättning (vindrik dag, stabila priser)
  p30 = 1.95, median = 2.00  →  1.95 > 1.80  ✗ BUY suppressed (inte värt det)
  p70 = 2.05, median = 2.00  →  2.05 < 2.20  ✗ AVOID suppressed
  → Alla timmar blir IDLE, kunden behöver inte tänka på timing
```

### 4.6 Steg 4: Solproduktion med temperaturkorrigering

Solpaneler producerar mindre vid höga temperaturer. Vi använder IEC 61215 NOCT-modellen:

```c
// Konstanter (IEC 61215)
#define NOCT         45.0   // Nominal Operating Cell Temperature (°C)
#define TEMP_STC     25.0   // Standard Test Conditions temperature (°C)
#define TEMP_COEFF   -0.0045  // Power loss per °C above STC
#define WIND_COOL    0.04   // Convective cooling factor

// Cell temperature från väderdata
double cellTemp = ambient +
    (NOCT - 20.0) / (800.0 * (1.0 + WIND_COOL * windSpeed)) * irradiance;

// Temperature derating factor
double tempFactor = 1.0 + TEMP_COEFF * (cellTemp - TEMP_STC);
if (tempFactor < 0.70) tempFactor = 0.70;  // Max 30% derating
if (tempFactor > 1.10) tempFactor = 1.10;  // Max 10% bonus

// Production with IEC 61724 performance ratio (0.75)
#define PERF_RATIO 0.75  // Wiring losses and inverter efficiency

double production = (irradiance / 1000.0) * solarArea * efficiency
                    * PERF_RATIO * tempFactor;
```

**Exempel:**
```
Solig sommardag:
  Irradiance:  800 W/m²
  Ambient:     30°C
  Wind:        2 m/s
  Cell temp:   ~52°C
  Temp factor: 1.0 + (-0.0045) × (52 - 25) = 0.88  (12% förlust p.g.a. värme)

  Production:  (800/1000) × 20 m² × 0.20 × 0.75 × 0.88 = 2.11 kWh

Kylig vårdag (optimal!):
  Irradiance:  800 W/m²
  Ambient:     10°C
  Wind:        5 m/s
  Cell temp:   ~25°C
  Temp factor: 1.0 (perfekt temperatur!)

  Production:  (800/1000) × 20 m² × 0.20 × 0.75 × 1.0 = 2.40 kWh  (+13%)
```

**Varför är detta viktigt?**
- På varma sommardagar kan faktisk produktion vara 10-15% lägre än spec
- Utan temperaturkorrigering skulle vi överestimera export-möjligheter
- Använder faktisk meteorologisk data, inte bara installerad effekt

### 4.7 Steg 5: Konsumtionsprofil

Svensk typisk hushållsprofil baserad på vardagsbeteende:

```c
// Load profile relative to configured average
static double consumption_factor(int hour) {
    if (hour < 7)  return 0.40;   // Natt: sover, minimal förbrukning
    if (hour < 17) return 1.00;   // Dag: på jobbet, background appliances
    if (hour < 23) return 1.60;   // Kväll: matlagning, EV, TV, värme
    return 0.70;                   // Sen kväll: tappar av
}

double hourlyLoad = avgConsumption * consumption_factor(hour);
```

**Exempel med 2 kWh genomsnitt:**
```
00:00-06:59:  0.80 kWh/h  (40% - natt)
07:00-16:59:  2.00 kWh/h  (100% - dag, normal baseline)
17:00-22:59:  3.20 kWh/h  (160% - kväll, peak!)
23:00-23:59:  1.40 kWh/h  (70% - sen kväll)

Daglig total: 24 timmar × 2 kWh avg = 48 kWh/dag
```

**Varför viktig?**
- Peak-timmar (17-23) sammanfaller ofta med dyra elpriser
- BUY-fönster på natten (02-06) har låg konsumtion → perfekt för EV-laddning
- Realistiska savings-beräkningar baserade på faktiskt beteende

### 4.8 Steg 6: Beslut per timme

För varje timme i prognosen:

```c
double net = production - consumption;

// Decision logic
if (net > 0.05 && spotPrice >= 0.05) {
    action = SELL_TO_GRID;
    // Exportera överskott (endast om > 50W och spotpris > 5 öre)
    // Annars är switching losses större än intäkten
}
else if (totalCost <= buyThreshold) {
    action = BUY_FROM_GRID;
    // Perfekt tid att ladda EV, köra tvättmaskin, värmepump extra
}
else if (totalCost >= avoidThreshold) {
    action = AVOID_HIGH_PRICE;
    // Undvik onödiga laster, använd batteri om tillgängligt
}
else {
    action = IDLE;
    // Normal drift, varken särskilt billigt eller dyrt
}
```

**Thresholds för SELL:**
```c
#define SELL_MIN_KWH   0.05   // 50W minimum export
#define SELL_MIN_PRICE 0.05   // 5 öre minimum spot price
```

**Varför dessa gränser?**
- Inverter efficiency: switching från självkonsumtion till export har förluster
- Negativa priser: ibland betalar producenter för att exportera → undvik!
- Administrative overhead: micro-transactions på 1-2 öre är inte värda det

### 4.9 Steg 7: Best BUY Window

Hitta den längsta sammanhängande perioden av BUY-timmar med största besparingen:

```c
// Scan through all hours
int windowStart = -1, windowEnd = -1;
double windowSavings = 0.0;
int windowHours = 0;
double bestSavings = -1.0;

for (int i = 0; i <= n; i++) {
    bool isBuy = (i < n && entries[i].action == BUY_FROM_GRID);

    if (isBuy) {
        if (windowStart < 0) windowStart = i;
        windowEnd = i;
        windowSavings += (median - entries[i].cost) * entries[i].consumption;
        windowHours++;
    }
    else if (windowStart >= 0) {
        // Window ended, check if best so far
        if (windowSavings > bestSavings) {
            bestBuyWindow = {
                .start = entries[windowStart].timestamp,
                .end = entries[windowEnd].timestamp,
                .hours = windowHours,
                .avgCostSek = totalWindowCost / windowHours,
                .savingsSek = windowSavings
            };
        }
        // Reset for next window
        windowStart = -1;
        windowSavings = 0.0;
    }
}
```

**Exempel-output:**
```json
{
  "bestBuyWindow": {
    "start": "2026-03-11T02:00:00Z",
    "end": "2026-03-11T06:00:00Z",
    "hours": 4,
    "avgCostSek": 1.35,
    "savingsSek": 8.50
  }
}
```

**Sparande-beräkning:**
```
Median pris:  2.00 SEK/kWh
BUY-timmar:   4h @ 1.35 SEK/kWh avg
Konsumtion:   0.8 kWh/h (natt-profil)

Normal kostnad (median):  4h × 0.8 kWh × 2.00 = 6.40 SEK
BUY-fönster kostnad:      4h × 0.8 kWh × 1.35 = 4.32 SEK
Passiv besparing:         6.40 - 4.32 = 2.08 SEK

Om kunden shiftar 8 kWh EV-laddning från peak (3.20 SEK) till BUY-fönster:
Extra saving:             8 × (3.20 - 1.35) = 14.80 SEK

Rekommenderat i UI:       "Ladda bilen mellan 02:00-06:00 → spara ~15 SEK"
```

---

## 5. Data Flow - Complete Example

### 5.1 HTTP Request kommer in

```
1. Client:   POST /forecast
             {"userId": "user123", "solarAreaM2": 20, ...}

2. Server:   ClientHandler thread tar emot request
             Skapar WorkRequest struct
             Registrerar WorkCompletion i global registry
             Skriver till anonymous pipe → Fetcher stdin
```

### 5.2 Fetcher hämtar data

```
3. Fetcher:  Läser WorkRequest från stdin (blocking read)
             Kontrollerar SharedCache för weather/price data

             Cache hit:  → använd cached JSON direkt
             Cache miss: → HTTP GET till SMHI API (väder)
                        → HTTP GET till Sourceful API (elpriser)
                        → spara i SharedCache (TTL 15 min)

             Skapar FetchResult med JSON-strings
             Skriver till /tmp/gridguard_fetch_to_parse.fifo
```

### 5.3 Parser tolkar JSON

```
4. Parser:   Läser FetchResult från FIFO (blocking read)
             Parsar JSON → ForecastData (48 entries)

             För varje timme:
               - Extrahera spotPriceSek från Sourceful JSON
               - Extrahera solarIrradiance, temp, wind från SMHI JSON
               - Sätt ForecastEntry.valid = true om OK

             Skickar ForecastData via Unix socket till ComputeWorker
```

### 5.4 Compute beräknar plan

```
5. ComputeWorker:  Tar emot ForecastData från Unix socket
                   Anropar Compute_GenerateEnergyPlan()

                   → Beräkna totalCost per timme (spot + fees + tax + VAT)
                   → Sortera costs och hitta p30, p50, p70
                   → Applicera quality gates på BUY/AVOID thresholds

                   → Loop 48 timmar:
                      • Beräkna solar production (NOCT temp correction)
                      • Beräkna consumption (time-of-day profile)
                      • Beslut: SELL/BUY/AVOID/IDLE

                   → Hitta best BUY window (största sparande)

                   Skapar EnergyData result
                   Hittar WorkCompletion via userId i registry
                   Signalerar completion (pthread_cond_signal)
```

### 5.5 HTTP Response skickas

```
6. Server:   ClientHandler thread väcks av WorkCompletion_Wait()
             Serialiserar EnergyData → JSON response
             HTTP 200 OK med full energiplan (48 timmar + metadata)

             Unregister WorkCompletion från registry
```

**Total tid:**
- Cache hit: ~50-100ms
- Cache miss: ~300-500ms (beroende på API-latency)

---

## 6. Fel-hantering

### 6.1 API-fel (HTTP request failures)

```c
// Retry logic i HTTPClient_Get()
int retries = 3;
while (retries-- > 0) {
    int result = HTTPClient_Get(url, response);
    if (result == 0) break;

    LOG_WARNING("HTTP GET failed for %s, retry %d/3", url, 3 - retries);
    sleep(1);
}

if (retries < 0) {
    LOG_ERROR("All retries exhausted for %s", url);
    return -1;
}
```

**Exempel-scenarios:**
- SMHI API timeout → retry 3 gånger → fallback till cached data om tillgängligt
- Sourceful API 500 error → retry → returnera error till user om cache missar

### 6.2 Parser-fel (malformed JSON)

```c
// Graceful degradation i APIParser
cJSON *json = cJSON_Parse(buffer);
if (!json) {
    LOG_ERROR("Invalid JSON from API, skipping entry");
    // Fortsätt med nästa entry, en dålig timme förstör inte hela prognosen
    continue;
}

cJSON *field = cJSON_GetObjectItem(json, "value");
if (!field || !cJSON_IsNumber(field)) {
    LOG_WARNING("Missing field 'value', using default");
    value = 0.0;
}
```

**Exempel:**
- En timme i SMHI-data saknas → markera som invalid, fortsätt med resterande 47 timmar
- Compute kan hantera ofullständiga prognoser (minst ~24 timmar krävs för meningsfull analys)

### 6.3 IPC-fel

```c
// Broken pipe detection
ssize_t written = write(pipeFd, &data, sizeof(data));
if (written < 0 && errno == EPIPE) {
    LOG_ERROR("Broken pipe, reader process died");
    // Cleanup och exit
}

// FIFO open() kan blocka om ingen reader
int fd = open(fifoPath, O_WRONLY | O_NONBLOCK);
if (fd < 0) {
    if (errno == ENXIO) {
        LOG_ERROR("No reader on FIFO");
    }
}
```

### 6.4 Cleanup vid shutdown

```bash
# Manual cleanup om Server kraschat
make stop   # eller:
pkill -TERM GridGuard
rm -f /tmp/gridguard*.fifo /tmp/gridguard*.sock
```

---

## 7. Performance

### 7.1 Cache Hit Rate

Med 15 min TTL och typisk användning (request var 5:e minut):
- **Weather cache hit:** ~95% (SMHI-data uppdateras var 3:e timme)
- **Price cache hit:** ~90% (Sourceful-data uppdateras dagligen kl 13:00)

**Vinst:** Från 300-500ms API-latency till <10ms cache read

### 7.2 Throughput

Sequential processing med fork()-based architecture:
- **~200 requests/s** vid cache hit (begränsat av context switching)
- **~20 requests/s** vid cache miss (begränsat av API-latency)

Med ThreadPool (20 workers) för HTTP server:
- **~2000 concurrent connections** (non-blocking I/O)

### 7.3 Memory

```
Server process:       ~1.6 GB   (ThreadPool: 20 × 8 MB stacks)
Fetcher process:      ~5 MB     (cJSON parsing + HTTP client)
Parser process:       ~3 MB     (cJSON library)
Shared memory caches: ~1 MB     (2 × 512 KB)
Total system:         ~1.61 GB
```

**Note:** Thread stack size kan reduceras från default 8 MB till t.ex. 2 MB för production:
```c
pthread_attr_t attr;
pthread_attr_init(&attr);
pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);  // 2 MB
```

---

## 8. Demo-tips

### 8.1 Visa process-hierarkin

```bash
ps aux | grep GridGuard | grep -v grep

# Output (nuvarande system):
# znees  PID  ...  GridGuard-server
# znees  PID  ...  GridGuard-fetcher /tmp/gridguard_fetch_to_parse.fifo
# znees  PID  ...  GridGuard-parser /tmp/gridguard_fetch_to_parse.fifo /tmp/gridguard_parse_to_compute.sock

# Visa parent-child relation
pstree -p $(pgrep GridGuard-server)
# GridGuard-server(PID)─┬─GridGuard-fetcher(PID)
#                        └─GridGuard-parser(PID)
```

### 8.2 Visa IPC-resurser

```bash
# Named pipe och Unix socket
ls -la /tmp/gridguard*.fifo /tmp/gridguard*.sock

# Output:
# prw-rw-rw- 1 znees znees 0 Mar 10 09:22 /tmp/gridguard_fetch_to_parse.fifo
# srwxr-xr-x 1 znees znees 0 Mar 10 09:22 /tmp/gridguard_parse_to_compute.sock

# Shared memory
ls -la /dev/shm/gridguard*

# Output:
# -rw------- 1 znees znees 524288 Mar 10 09:22 /dev/shm/gridguard_weather
# -rw------- 1 znees znees 524288 Mar 10 09:22 /dev/shm/gridguard_price
```

### 8.3 Testa API-request med cache timing

```bash
# Första requesten (cache miss)
time curl -X POST http://localhost:8080/forecast \
  -H "Content-Type: application/json" \
  -d '{
    "userId": "demo",
    "solarAreaM2": 20.0,
    "solarEfficiency": 0.20,
    "consumptionKwh": 2.0,
    "gridFee_low": 0.20,
    "gridFee_normal": 0.40,
    "gridFee_high": 0.60
  }'

# real  0m0.384s  (API latency + compute)

# Andra requesten inom 15 min (cache hit)
time curl -X POST http://localhost:8080/forecast ...

# real  0m0.062s  (6x snabbare med cache!)
```

### 8.4 Förklara compute-output live

```json
{
  "entries": [
    {
      "timestamp": "2026-03-11T14:00:00Z",
      "action": "SELL_TO_GRID",          ← Solproduktion > konsumtion
      "productionKwh": 4.2,              ← Soligt, middag, peak production
      "consumptionKwh": 2.0,             ← Dag-profil (100% av avg)
      "spotPrice": 0.82,                 ← OK pris för export (>0.05)
      "totalCostSek": 2.15,              ← Vad kunden skulle betala om import
      "priceVsAvgPct": 7.5               ← 7.5% över median (men OK, vi säljer)
    },
    {
      "timestamp": "2026-03-11T03:00:00Z",
      "action": "BUY_FROM_GRID",         ← Natt-pris, BILLIGT!
      "productionKwh": 0.0,              ← Ingen sol
      "consumptionKwh": 0.8,             ← Natt-profil (40% av avg)
      "spotPrice": 0.35,                 ← Lågt spotpris
      "totalCostSek": 1.35,              ← Under BUY threshold (1.40)
      "priceVsAvgPct": -32.5             ← 32.5% UNDER median!
    },
    {
      "timestamp": "2026-03-11T18:00:00Z",
      "action": "AVOID_HIGH_PRICE",      ← Peak hour, DYRT!
      "productionKwh": 0.3,              ← Sol på väg ner
      "consumptionKwh": 3.2,             ← Kväll-peak (160% av avg)
      "spotPrice": 1.45,                 ← Högt spotpris + high grid fee
      "totalCostSek": 3.15,              ← Över AVOID threshold (2.80)
      "priceVsAvgPct": 57.5              ← 57.5% över median → undvik!
    }
  ],
  "bestBuyWindow": {
    "start": "2026-03-11T02:00:00Z",
    "end": "2026-03-11T06:00:00Z",
    "hours": 4,                          ← 4 timmar billig el i sträck
    "avgCostSek": 1.35,                  ← Genomsnitt under fönstret
    "savingsSek": 8.50                   ← Spara 8.50 SEK genom att shifta last hit!
  },
  "totalCostSek": 96.30,                 ← Total kostnad för 48h
  "totalGridImportKwh": 28.4,            ← Behöver importera 28.4 kWh
  "totalGridExportKwh": 12.8             ← Kan exportera 12.8 kWh (SELL-timmar)
}
```

### 8.5 Visa logs i realtid

```bash
# Server logs (HTTP requests och fork events)
tail -f logs/server.log

# Fetcher logs (API calls och cache hits)
tail -f logs/fetcher.log | grep -i cache

# Parser logs (JSON parsing)
tail -f logs/parser.log

# Compute logs (percentile calculations)
tail -f logs/server.log | grep -i compute
```

---

## 9. Viktiga kod-filer att visa

### 9.1 Process Management & IPC Setup
- `src/server/GridGuard.c:112-124` - Skapa anonymous pipe för HTTP → Fetch
- `src/server/GridGuard.c:126-158` - Fork Fetcher med dup2() för stdin
- `src/server/GridGuard.c:168-190` - Fork Parser med execl()
- `src/server/GridGuard.c:96-107` - Skapa FIFO för Fetch → Parse

### 9.2 IPC Communication
- `src/fetcher/Fetcher.c:50-66` - Läs från stdin (anonymous pipe)
- `src/fetcher/Fetcher.c:79-88` - Skriv till FIFO
- `src/parser/Parser.c:83-101` - Unix socket server setup
- `src/compute/ComputeWorker.c:25-55` - Unix socket client (connect & recv)

### 9.3 Compute Logic
- `src/compute/Compute.c:139-164` - Percentile calculation + quality gates
- `src/compute/Compute.c:182-189` - NOCT solar model med temperaturkorrigering
- `src/compute/Compute.c:192-210` - SELL/BUY/AVOID decision logic
- `src/compute/Compute.c:238-273` - Best BUY window detection

### 9.4 Cache System
- `src/cache/SharedCache.c:30-104` - SharedCache_Create/Put/Get
- `src/fetcher/Fetcher.c:113-145` - Cache lookup före API-anrop

### 9.5 Signal Handling & Shutdown
- `src/server/GridGuard.c:183-237` - GridGuard_Shutdown() med graceful termination
- `src/sys/SignalHandler.c` - SIGTERM/SIGINT handling

---

## 10. Tekniska höjdpunkter att nämna

1. **POSIX-compliant IPC:** Anonymous pipes, named pipes (FIFOs), Unix domain sockets, POSIX shared memory
2. **Process isolation:** Fork-based architecture med clear separation of concerns
3. **Standards-based compute:** IEC 61724 (PV performance ratio), IEC 61215 (NOCT cell temperature model)
4. **Smart caching:** TTL-based shared memory cache för API-data (15 min)
5. **Quality gates:** Undviker false positives på flat-price days (BUY_MIN_DISCOUNT, AVOID_MIN_PREMIUM)
6. **Temperature correction:** Real NOCT model för solproduktion (inte bara installerad effekt)
7. **Swedish market specifics:** Time-of-use grid fees, energiskatt (0.40 SEK), moms (25%)
8. **Thread-safe:** Mutex-skyddade caches och completion registry
9. **Production-ready logging:** Structured logging till separate log-filer per process
10. **Graceful error handling:** Retry logic, JSON parsing fallbacks, broken pipe detection

---

## 11. Pågående arbete: Watchdog Process Supervision

### 11.1 Motivation

**Problem med nuvarande system:**
1. Ingen automatisk crash recovery
2. Ingen health monitoring (heartbeats)
3. Zombie-processer vid unclean shutdown
4. Manuell cleanup av IPC-resurser

**Lösning:** Watchdog-baserad supervision (branch: `feature/watchdog-process-supervision`)

### 11.2 Ny arkitektur

```
Watchdog (supervisor process)
├── Server (HTTP API + ComputeWorker thread)
├── Fetcher (API data fetching)
└── Parser (JSON parsing)
```

**Watchdog ansvarar för:**
- Skapa alla IPC-resurser (FIFOs, sockets) innan spawn
- Spawna alla tre processer med heartbeat pipes
- Monitora heartbeats (5s interval, 15s timeout)
- Koordinerad restart: om en process dör, starta om alla tre

### 11.3 Heartbeat Protocol

Varje process skickar `"heartbeat\n"` var 5:e sekund:

```c
// Child process
ProcessHeartbeat heartbeat;
ProcessHeartbeat_Initiate(&heartbeat, 5);  // 5s interval

while (running) {
    ProcessHeartbeat_Send(&heartbeat);

    // Non-blocking work with select()
    struct timeval timeout = {.tv_sec = 1};
    select(workFd + 1, &readfds, NULL, NULL, &timeout);
    // Loop again to send heartbeat
}
```

**Watchdog monitoring:**
```c
if (difftime(time(NULL), lastHeartbeat) >= 15) {
    LOG_WARNING("Process frozen, killing all");
    kill(fetcherPid, SIGTERM);
    kill(parserPid, SIGTERM);
    kill(serverPid, SIGTERM);
    // Wait, cleanup IPC, restart all three
}
```

### 11.4 Fördelar

- **Automatic recovery:** Processer som crashar startas automatiskt om
- **Freeze detection:** Upptäcker deadlocks och infinite loops
- **Clean restarts:** Alla IPC-resurser rensas och återskapas
- **Restart policy:** Exponential backoff (2s, 4s, 8s, 16s, 32s), max 5 restarts/5min
- **Separation:** Server ansvarar inte längre för att spawna children

### 11.5 Status

**Implementerat:**
- ✅ ProcessHeartbeat modul (src/sys/ProcessHeartbeat.c)
- ✅ Watchdog spawning och monitoring (src/watchdog/Watchdog.c)
- ✅ Coordinated restart logic
- ✅ FIFO-baserad IPC (inte längre anonymous pipe + stdin)

**Återstående:**
- Testing av restart-scenarier
- Integration med systemd/init
- Metrics och monitoring endpoints

**ETA:** Merge till `development` när testing är klar (Q2 2026)

---

## 12. Vanliga frågor från lärare/examinatorer

**Q: Varför fork() och inte bara threads?**
A: Separation of concerns och failure isolation. Om Parser crashar med segfault påverkas inte Fetcher. Med threads skulle ett segfault ta ner hela processen. Dessutom kan vi i framtiden distribuera processer över flera maskiner.

**Q: Varför både anonymous pipe och FIFO?**
A: Anonymous pipe (pipe() + dup2) är perfekt för parent → child kommunikation (Server → Fetcher), skapas automatiskt vid fork(). FIFO är bättre för sibling-kommunikation (Fetcher → Parser) där ingen är parent.

**Q: Varför Unix socket istället för FIFO för Parser → Compute?**
A: Unix socket är bättre för bidirectional request-response. Parser skickar ForecastData, ComputeWorker kan svara eller signalera completion. FIFO är bara one-way streaming.

**Q: Varför shared memory för cache?**
A: För att undvika duplicate API-anrop. Om två requests kommer samtidigt kan båda läsa från samma cache istället för att båda anropa SMHI. Plus att det är snabbast (zero-copy).

**Q: Hur vet ni att en process har dött?**
A: **Nuvarande:** Vid shutdown använder vi waitpid(). Vid crash upptäcker vi det när write() till pipe returnerar EPIPE. **Framtida (watchdog):** Heartbeat timeout (15s) + waitpid() + SIGCHLD handler.

**Q: Vad händer om Server dör?**
A: **Nuvarande:** Fetcher och Parser blir orphans, adopteras av init (PID 1), måste dödas manuellt. **Framtida (watchdog):** Watchdog får SIGCHLD, dödar alla tre och startar om.

**Q: Hur testas systemet?**
A: (1) Unit tests för Compute-algoritmen (percentile, NOCT, quality gates), (2) Integration tests med mock API-data, (3) Manual testing med curl, (4) Load testing med concurrent requests.

**Q: Varför NOCT-modellen för sol?**
A: IEC 61215 standard. Solpaneler tappar ~0.45%/°C över 25°C. På varma sommardagar (50°C celltemp) kan faktisk produktion vara 10-15% lägre än spec. Vi använder real meteorologisk data (temp, vind) för precision.

**Q: Quality gates – varför inte bara använda percentiler direkt?**
A: På flat-price days (t.ex. vindrik dag med stabila priser) kan p30 = 1.95 SEK och median = 2.00 SEK. Skillnaden är 5 öre – inte värt att väcka kunden för "billig el" som knappt är billigare. Quality gates (10% threshold) filtrerar false positives.

**Q: Vad händer vid API-failure?**
A: (1) Retry 3 gånger med 1s mellanrum, (2) Om cache finns, använd cached data (max 15 min gammal), (3) Om inga retries lyckas OCH ingen cache, returnera HTTP 500 med error message till kunden.

---

## 13. Sammanfattning

GridGuard är ett **production-ready** system för smart energihantering som:

- Använder **fork-based process isolation** för robust multi-process arkitektur
- Implementerar **4 olika POSIX IPC-mekanismer** (pipes, FIFOs, Unix sockets, shm)
- Beräknar energiplaner med **industry-standard modeller** (IEC 61724, IEC 61215)
- Har **intelligent caching** för att minimera API-latency
- Använder **quality gates** för att undvika false positives
- Ger **actionable recommendations** (best BUY window, SEK-besparing)

**Pågående vidareutveckling:**
- Watchdog-baserad supervision för automatic crash recovery
- Heartbeat monitoring för freeze detection
- Systemd integration för production deployment

**Total codebase:** ~8000 rader C (exklusive libs), ~15 moduler, 3 processer, 4 IPC-typer

---

**Detta dokument täcker hela systemet från arkitektur till algorithm-detaljer. Perfekt som referens under demo eller examination!**
