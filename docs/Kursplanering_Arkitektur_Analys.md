# Systemarkitektur baserad på Kursplanering.pdf

## Frågan: Hade jag designat annorlunda med kursplaneringen i åtanke?

**Kort svar: JA - men inte för att nuvarande design är fel, utan för att maximera demonstrationen av ALLA kursmål.**

---

## Vad kursen faktiskt undervisar (Vecka för vecka)

### Vecka 1: Processer
- `fork()`, `exec()`, `wait()`, `waitpid()`
- Processhantering i Linux

### Vecka 2: Trådar (pthreads)
- `pthread_create()`, `pthread_join()`
- Parallella beräkningar

### Vecka 3: Synkronisering
- **Mutex**: `pthread_mutex_t`
- **Condition variables**: `pthread_cond_t`
- **Producer-consumer pattern**
- Race conditions, deadlocks

### Vecka 4: Pipes och IPC
- **Anonyma pipes**: `pipe()`
- **Named pipes (FIFO)**: `mkfifo()`
- `dup2()` för I/O-omdirigering
- **Bygga pipelines mellan processer**

### Vecka 5: Delat minne och sockets
- **Unix domain sockets**: `socket()`, `bind()`, `listen()`, `accept()`
- **POSIX shared memory**: `shm_open()`, `mmap()`
- **POSIX semaforer**: `sem_open()`, `sem_wait()`, `sem_post()`

### Vecka 6-11: C++ (inte relevant här)

### Vecka 10-11: Profilering och optimering
- `gprof`, `perf`, `valgrind`
- Cache-analys
- Benchmarking

---

## Nuvarande arkitektur: Vad täcks?

### ✅ Vad vi har idag

```
HTTP Server (ThreadPool)
    ↓
FetchWorker (pthread) → Queue → ParseWorker (pthread) → Queue → ComputeWorker (pthread)
    ↓                                                                  ↓
WorkCompletion (mutex + cond)                          WorkCompletion_Signal()
SharedCache (shm_open + mmap + semaphore)
```

**Kursbegrepp som täcks:**

| Vecka | Begrepp | Täcks? | Hur? |
|-------|---------|--------|------|
| 1 | Processer, fork/exec | ❌ NEJ | Allt är trådar, inga processer |
| 2 | Trådar (pthreads) | ✅ JA | FetchWorker, ParseWorker, ComputeWorker |
| 3 | Mutex + condition variables | ✅ JA | WorkCompletion, Queue |
| 3 | Producer-consumer | ✅ JA | Alla tre köer |
| 4 | Pipes (anonyma) | ❌ NEJ | Använder minnesbaserade köer |
| 4 | Named pipes (FIFO) | ❌ NEJ | - |
| 4 | dup2() och pipelines | ❌ NEJ | - |
| 5 | Unix domain sockets | ❌ NEJ | Använder TCP sockets (inte Unix domain) |
| 5 | Shared memory | ✅ JA | SharedCache med shm_open/mmap |
| 5 | POSIX semaforer | ✅ JA | SharedCache använder sem_open |

**Täckning: 5/9 kurskoncept = 56%**

---

## Hybrid arkitektur som täcker ALLT

### Design-principen:

**Använd BÅDE processer OCH trådar för att demonstrera hela spektrumet av IPC-tekniker.**

```
┌──────────────────────────────────────────────────────────────┐
│                    GridGuard System                           │
└──────────────────────────────────────────────────────────────┘

┌─────────────────┐
│  Main Process   │  ← fork() skapar child-processer
│  (HTTP Server)  │
└────────┬────────┘
         │
         ├─── fork() ───> ┌──────────────────┐
         │                │  Fetch Process   │ ← Dedikerad process
         │                └────────┬─────────┘
         │                         │
         │                    Named PIPE (FIFO)
         │                         │
         ├─── fork() ───> ┌────────▼─────────┐
         │                │  Parse Process   │ ← Dedikerad process
         │                └────────┬─────────┘
         │                         │
         │                   Unix Socket
         │                         │
         └─── pthread ──> ┌────────▼─────────┐
                          │ Compute Thread   │ ← Tråd i main process
                          └──────────────────┘

         Shared mellan processer:
         ┌────────────────────────────┐
         │ Shared Memory Cache        │ ← shm_open/mmap + semaphore
         │ (Weather + Price data)     │
         └────────────────────────────┘
```

### Komponenter:

#### 1. **Main Process - HTTP Server** (Vecka 1-2)
```c
int main(void) {
    // Fork child processes
    pid_t fetchPid = fork();
    if (fetchPid == 0) {
        // Child: Fetch process
        execl("./bin/gridguard-fetcher", "gridguard-fetcher", NULL);
        exit(1);
    }

    pid_t parsePid = fork();
    if (parsePid == 0) {
        // Child: Parse process
        execl("./bin/gridguard-parser", "gridguard-parser", NULL);
        exit(1);
    }

    // Main process: HTTP server + Compute thread
    pthread_t computeThread;
    pthread_create(&computeThread, NULL, ComputeWorker_Run, NULL);

    // HTTP server loop...
    HTTPServer_Run();

    // Cleanup
    pthread_join(computeThread, NULL);
    waitpid(fetchPid, NULL, 0);
    waitpid(parsePid, NULL, 0);
}
```

**Visar:**
- ✅ `fork()` - skapa child-processer
- ✅ `exec()` - starta dedikerade program
- ✅ `wait()` / `waitpid()` - vänta på child-processer
- ✅ `pthread_create()` - blanda processer och trådar

---

#### 2. **HTTP → Fetch: Anonyma Pipes** (Vecka 4)

```c
// main.c
int pipefds[2];
pipe(pipefds);  // Skapa pipe INNAN fork()

pid_t fetchPid = fork();
if (fetchPid == 0) {
    // Child: Fetch process
    close(pipefds[1]);  // Stäng write-end
    dup2(pipefds[0], STDIN_FILENO);  // Stdin = pipe read-end
    close(pipefds[0]);

    execl("./bin/gridguard-fetcher", "gridguard-fetcher", NULL);
    exit(1);
}

// Parent: HTTP server
close(pipefds[0]);  // Stäng read-end

// När HTTP-request kommer in:
void HandleHTTPRequest(int clientFd) {
    WorkRequest req = {...};

    // Skriv request till pipe → fetch process läser från stdin
    write(pipefds[1], &req, sizeof(req));

    // ... vänta på svar via annat IPC ...
}
```

**Visar:**
- ✅ `pipe()` - skapa anonyma pipes
- ✅ `dup2()` - redirect stdin till pipe
- ✅ Enkelriktad kommunikation
- ✅ Fork + file descriptor inheritance

---

#### 3. **Fetch → Parse: Named Pipes (FIFO)** (Vecka 4)

```c
// setup.c - körs vid systemstart
mkfifo("/tmp/gridguard_fetch_to_parse.fifo", 0666);

// gridguard-fetcher (fetch process)
int main(void) {
    int fifo_fd = open("/tmp/gridguard_fetch_to_parse.fifo", O_WRONLY);

    while (1) {
        WorkRequest req;
        // Läs från stdin (pipe från HTTP server)
        read(STDIN_FILENO, &req, sizeof(req));

        // Fetch data
        FetchResult result = FetchWeatherAndPrice(&req);

        // Skriv till FIFO → parse process
        write(fifo_fd, &result, sizeof(result));
    }
}

// gridguard-parser (parse process)
int main(void) {
    int fifo_fd = open("/tmp/gridguard_fetch_to_parse.fifo", O_RDONLY);

    while (1) {
        FetchResult result;
        // Läs från FIFO (blockerar tills data finns)
        read(fifo_fd, &result, sizeof(result));

        // Parse data...
        ParseResult parsed = ParseData(&result);

        // Skicka vidare till Compute...
    }
}
```

**Visar:**
- ✅ `mkfifo()` - skapa named pipe
- ✅ Kommunikation mellan **oberoende processer**
- ✅ Blockande I/O på pipes
- ✅ FIFO-semantik (first in, first out)

---

#### 4. **Parse → Compute: Unix Domain Sockets** (Vecka 5)

```c
// gridguard-parser (server-sida)
int main(void) {
    int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/tmp/gridguard.sock", sizeof(addr.sun_path) - 1);

    unlink(addr.sun_path);  // Remove old socket
    bind(sockfd, (struct sockaddr*)&addr, sizeof(addr));
    listen(sockfd, 5);

    while (1) {
        int client = accept(sockfd, NULL, NULL);

        ParseResult parsed = ...;
        write(client, &parsed, sizeof(parsed));
        close(client);
    }
}

// Compute thread (client-sida, i main process)
void *ComputeWorker_Run(void *arg) {
    while (1) {
        int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);

        struct sockaddr_un addr = {0};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, "/tmp/gridguard.sock", sizeof(addr.sun_path) - 1);

        connect(sockfd, (struct sockaddr*)&addr, sizeof(addr));

        ParseResult parsed;
        read(sockfd, &parsed, sizeof(parsed));
        close(sockfd);

        // Compute...
        EnergyPlan plan = ComputeEnergyPlan(&parsed);

        // Signal WorkCompletion...
    }
}
```

**Visar:**
- ✅ `socket(AF_UNIX)` - Unix domain sockets
- ✅ `bind()`, `listen()`, `accept()`, `connect()`
- ✅ Lokal IPC (snabbare än TCP)
- ✅ Client-server pattern mellan process och tråd

---

#### 5. **Shared Memory Cache** (Vecka 5) - BEHÅLLS SOM DET ÄR

```c
// Samma som nuvarande SharedCache.c
SharedCache weatherCache;
SharedCache_Create(&weatherCache, "/gridguard_weather", 900);

// Alla processer kan läsa/skriva
SharedCache_Store(&weatherCache, "stockholm_weather", jsonData);
SharedCache_Lookup(&weatherCache, "stockholm_weather", buffer, sizeof(buffer));
```

**Visar:**
- ✅ `shm_open()` - POSIX shared memory
- ✅ `mmap()` - memory mapping
- ✅ `sem_open()`, `sem_wait()`, `sem_post()` - synkronisering

---

#### 6. **WorkCompletion inom Main Process** (Vecka 3) - BEHÅLLS

HTTP-tråd ↔ Compute-tråd använder WorkCompletion precis som nu.

**Visar:**
- ✅ `pthread_mutex_t` + `pthread_cond_t`
- ✅ Producer-consumer mellan trådar
- ✅ Condition variable signaling

---

## Fullständig kursbegrepp-täckning

| Vecka | Begrepp | Hybrid-arkitektur |
|-------|---------|-------------------|
| 1 | Processer, fork/exec | ✅ Main forkar Fetch + Parse |
| 1 | wait/waitpid | ✅ Main väntar på child-processer vid shutdown |
| 2 | pthread_create/join | ✅ Compute thread i main process |
| 3 | Mutex + cond | ✅ WorkCompletion (HTTP ↔ Compute) |
| 3 | Producer-consumer | ✅ WorkCompletion + alla IPC-kanaler |
| 4 | Anonyma pipes | ✅ HTTP → Fetch (via stdin/pipe) |
| 4 | Named pipes (FIFO) | ✅ Fetch → Parse |
| 4 | dup2() | ✅ Fetch redirectar stdin till pipe |
| 5 | Unix domain sockets | ✅ Parse → Compute |
| 5 | Shared memory | ✅ SharedCache (samma som nu) |
| 5 | POSIX semaforer | ✅ SharedCache (samma som nu) |

**Täckning: 11/11 = 100% av IPC-koncept! 🎯**

---

## Implementation: Katalogstruktur

```
GridGuard/
├── src/
│   ├── main.c              ← Main process: fork(), HTTP server
│   ├── fetcher/
│   │   └── main.c          ← Fetch process (läser stdin, skriver FIFO)
│   ├── parser/
│   │   └── main.c          ← Parse process (läser FIFO, Unix socket server)
│   └── compute/
│       └── compute.c       ← Compute thread (Unix socket client)
├── bin/
│   ├── gridguard-server    ← Main executable
│   ├── gridguard-fetcher   ← Fetch executable (exec:ad från main)
│   └── gridguard-parser    ← Parse executable (exec:ad från main)
```

### Makefile

```makefile
all: bin/gridguard-server bin/gridguard-fetcher bin/gridguard-parser

bin/gridguard-server: src/main.c src/compute/compute.c
	$(CC) -o $@ $^ -lpthread -lrt

bin/gridguard-fetcher: src/fetcher/main.c
	$(CC) -o $@ $^ -lcurl -lmbedtls

bin/gridguard-parser: src/parser/main.c
	$(CC) -o $@ $^ -lcjson
```

---

## Jämförelse: Nuvarande vs Hybrid

### Nuvarande (Thread-based Pipeline)

**Fördelar:**
- ✅ Enklare att implementera
- ✅ Delat minne naturligt (samma process)
- ✅ Snabbare (ingen process-switch overhead)
- ✅ Färre executables att hantera

**Nackdelar:**
- ❌ Täcker bara 56% av kursmålen
- ❌ Visar inte processer eller pipes
- ❌ Mindre "imponerande" för kursexamination

### Hybrid (Process + Thread Pipeline)

**Fördelar:**
- ✅ Täcker 100% av kursmålen
- ✅ Demonstrerar ALLA IPC-metoder
- ✅ Visar förståelse för process vs tråd trade-offs
- ✅ Mer imponerande på examination
- ✅ Mer "production-like" (microservices-liknande)

**Nackdelar:**
- ❌ Mer komplext att implementera
- ❌ Fler executables att bygga och deploya
- ❌ Något långsammare (IPC overhead)
- ❌ Svårare att debugga (flera processer)

---

## Min rekommendation för KURSPROJEKTET

### Option A: Behåll nuvarande (pragmatisk)

Om ni är **nöjda med nuvarande** och vill **fokusera på C++**-delen av kursen (vecka 6-11), behåll som det är.

**Men lägg till:**
1. **En Demo-branch** med pipe-baserad communication
2. **Dokumentation** som förklarar varför ni valde trådar över processer
3. **Benchmarks** som visar prestandaskillnaden

### Option B: Implementera Hybrid (maximal kurspoäng)

Om ni vill **maximera examinationsbetyget** och verkligen **visa full förståelse**:

1. **Refaktorera till Hybrid-arkitekturen**
2. Behåll **SharedCache** (visar shared memory)
3. Lägg till **pipe + FIFO + Unix socket** som ovan
4. **Dokumentera varje IPC-val** med referenser till kursmål

---

## Konkret implementationsplan (om ni vill byta)

### Vecka 1: Processer och pipes

```bash
git checkout -b feature/process-based-pipeline
```

**Steg:**
1. Skapa `src/fetcher/main.c` - läser från stdin (pipe)
2. Skapa anonyma pipes i `main.c`
3. Fork och exec fetcher-process
4. Test: HTTP → pipe → Fetch

### Vecka 2: FIFO

**Steg:**
1. `mkfifo("/tmp/gridguard_fetch_to_parse.fifo")`
2. Fetch skriver till FIFO
3. Parse läser från FIFO
4. Test: HTTP → pipe → Fetch → FIFO → Parse

### Vecka 3: Unix sockets

**Steg:**
1. Parse skapar Unix domain socket server
2. Compute (thread) connectar som client
3. Test: HTTP → ... → Parse → Unix socket → Compute
4. WorkCompletion för response

### Vecka 4: Integration och testing

**Steg:**
1. End-to-end test
2. Benchmarking vs nuvarande
3. Memory leak check med valgrind
4. Dokumentation

---

## Svar på din fråga: Vad skulle JAG göra?

### För KURSEN (maximal demonstration):

**JA, jag skulle designa Hybrid-arkitekturen** för att:
1. Täcka ALLA kursmål (100% vs 56%)
2. Visa djup förståelse för IPC-spektrumet
3. Differentiera projektet från andra grupper
4. Ge ammunition för reflektionsdelen i examinerationen

### Men:

**Nuvarande arkitektur är INTE fel.** Den är:
- Professionell
- Performant
- Production-ready för Linux-server
- Visar god systemdesign

Den visar bara inte **allt** som kursen lär ut.

---

## Bottom line

**Fråga:** Baserat på Kursplanering.pdf, hade du designat annorlunda?

**Svar:** JA - Hybrid arkitektur med:
- Main process forkar **Fetch** (exec) och **Parse** (exec)
- **HTTP → Fetch**: Anonyma pipes (pipe + dup2)
- **Fetch → Parse**: Named pipes (FIFO)
- **Parse → Compute**: Unix domain sockets
- **Shared memory cache**: Samma som nu (shm_open/mmap)
- **WorkCompletion**: Samma som nu (mutex/cond)

Detta ger **100% täckning** av kursmålen och maximerar examination-scores.

**Men:** Nuvarande design är fortfarande excellent och mer pragmatisk för production.

Vill ni att jag sketchar konkret kod för Hybrid-implementationen?
