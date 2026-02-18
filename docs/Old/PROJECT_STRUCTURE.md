# GridGuard Projektstruktur

*Uppdaterad: 2026-02-17*

## Översikt

GridGuard följer en lagerbaserad arkitektur som separerar olika ansvarsområden i distinkta mappar. Strukturen är designad för skalbarhet, tydlighet och följer industry best practices.

---

## Mappstruktur

```
GridGuard/
├── src/                      # Källkod
│   ├── application/          # Business logic & domain
│   ├── infrastructure/       # Platform & runtime
│   ├── network/              # Nätverkskommunikation
│   ├── concurrency/          # Concurrency primitives
│   ├── server/               # Server entry point
│   ├── client/               # Client entry point
│   ├── libs/                 # Third-party libraries
│   └── tests/                # Test suite
│
├── docs/                     # Dokumentation
├── build/                    # Kompilerade object files (gitignored)
├── bin/                      # Executables (gitignored)
├── logs/                     # Loggfiler (gitignored)
├── scripts/                  # Build och utility scripts
│
├── Makefile                  # Build system
└── README.md
```

---

## Detaljerad struktur

### 📦 src/application/ - Business Logic

Innehåller GridGuard-specifik logik - vad systemet gör.

```
src/application/
├── core/                     # Kärnan av applikationen
│   ├── GridGuard.c/h        # Huvudorchestrator för worker threads
│   └── (framtida core logic)
│
├── workers/                  # Worker threads
│   ├── FetchWorker.c/h      # Hämtar data från API:er
│   ├── ParseWorker.c/h      # Parsar JSON responses
│   ├── ComputeWorker.c/h    # Beräknar energiplaner
│   └── CacheWorker.c/h      # Hanterar caching och client response
│
├── services/                 # Business services
│   ├── Fetcher.c/h          # HTTP client wrapper (curl)
│   ├── Parser.c/h           # JSON parser wrapper (cJSON)
│   ├── Compute.c/h          # Energiberäkningar
│   └── Cache.c/h            # Cache management
│
├── models/                   # Data models
│   ├── EnergyData.c/h       # Energiplan struktur
│   ├── ForecastData.h       # Unified forecast data
│   ├── OpenMeteoData.h      # Väderdata från Open-Meteo
│   └── ElprisetData.h       # Spotpris från Elpriset
│
├── api/                      # External API integration
│   └── APIEndpoints.c/h     # URL builders för externa API:er
│
└── config/                   # Application configuration
    └── Config.h             # Konfigurationskonstanter
```

**Ansvar:** "Vad gör GridGuard?" - Affärslogik, domänmodeller och orchestration.

---

### 🏗️ src/infrastructure/ - Platform & Runtime

Plattformsnära kod som hanterar hur systemet körs.

```
src/infrastructure/
├── logging/                  # Logging subsystem
│   └── Logger.c/h           # Färgkodad konsoll-loggning
│
├── signals/                  # Signal handling
│   └── SignalHandler.c/h    # SIGINT/SIGTERM/SIGHUP handlers
│
├── daemon/                   # Daemon support (framtida)
│   └── (Daemon.c/h kommer här)
│
└── watchdog/                 # Watchdog process (framtida)
    └── (Watchdog.c/h kommer här)
```

**Ansvar:** "Hur körs GridGuard?" - Runtime, logging, process management, signals.

---

### 🌐 src/network/ - Nätverkskommunikation

Allt som rör TCP/IP kommunikation.

```
src/network/
├── server/                   # Server-side networking
│   └── TCPServer.c/h        # TCP socket server (listen, accept, send/recv)
│
└── client/                   # Client-side networking
    └── TCPClient.cpp/hpp    # C++ TCP client
```

**Ansvar:** "Hur kommunicerar GridGuard?" - Transport layer, sockets, protocols.

---

### ⚙️ src/concurrency/ - Concurrency Primitives

OS-nära concurrency mekanismer.

```
src/concurrency/
├── threads/                  # Thread management
│   ├── ThreadPool.c/h       # Worker thread pool för klienter
│   └── WorkerPool.c/h       # Generic worker pool implementation
│
├── sync/                     # Synchronization primitives
│   ├── Queue.c/h            # Trådsäker producer-consumer queue
│   └── Scheduler.c/h        # Task scheduling (framtida)
│
└── ipc/                      # Inter-process communication (framtida)
    └── (Pipes, shared memory kommer här)
```

**Ansvar:** "Hur parallelliseras GridGuard?" - Threads, mutexes, queues, IPC.

---

### 🖥️ src/server/ - Server Entry Point

Server entry point och orchestration.

```
src/server/
├── main.c                    # Entry point för servern
├── Server.c/h               # Äger alla komponenter (TCP, ThreadPool, GridGuard)
└── ClientHandler.c/h        # Client state machine och command parsing
```

**Ansvar:** Main entry point, top-level orchestration.

---

### 💻 src/client/ - Client Entry Point

C++ klient för att testa servern.

```
src/client/
└── main.cpp                  # C++ klient med TCP connection
```

**Ansvar:** Test client, future C++ features.

---

### 📚 src/libs/ - Third-party Libraries

Externa bibliotek som inte installeras systemwide.

```
src/libs/
└── cJSON.c/h                 # Lightweight JSON parser
```

---

### 🧪 src/tests/ - Test Suite

Tester organiserade efter typ.

```
src/tests/
├── unit/                     # Unit tests
│   └── test_logger.c        # Logger functionality test
│
└── integration/              # Integration tests
    ├── test_api_fetch.c     # API fetch + parsing test
    └── test_pipeline.c      # Multi-threaded pipeline test
```

---

## Arkitektur - Dataflöde

```
┌─────────────────────────────────────────────────────────────┐
│                        CLIENT                               │
└────────────────────────┬────────────────────────────────────┘
                         │ TCP Connection
                         ▼
┌─────────────────────────────────────────────────────────────┐
│  SERVER (src/server/)                                       │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ TCPServer (network/server/)                          │   │
│  │  ├── Accepts connections                             │   │
│  │  └── Hands off to ThreadPool                         │   │
│  └────────────┬─────────────────────────────────────────┘   │
│               ▼                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ ThreadPool (concurrency/threads/)                    │   │
│  │  ├── 20 worker threads                               │   │
│  │  ├── Each handles up to 50 clients                   │   │
│  │  └── select() for multiplexing                       │   │
│  └────────────┬─────────────────────────────────────────┘   │
│               ▼                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ ClientHandler (server/)                              │   │
│  │  ├── Parses commands                                 │   │
│  │  └── Submits WorkRequest                             │   │
│  └────────────┬─────────────────────────────────────────┘   │
└───────────────┼──────────────────────────────────────────────┘
                │ WorkRequest
                ▼
┌─────────────────────────────────────────────────────────────┐
│  GRIDGUARD PIPELINE (application/core/)                     │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ FetchWorker (application/workers/)                   │   │
│  │  ├── Reads from requestQueue                         │   │
│  │  ├── Fetches weather + spot price (Fetcher service)  │   │
│  │  └── Writes to fetchQueue                            │   │
│  └────────────┬─────────────────────────────────────────┘   │
│               ▼                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ ParseWorker (application/workers/)                   │   │
│  │  ├── Reads from fetchQueue                           │   │
│  │  ├── Parses JSON (Parser service)                    │   │
│  │  └── Writes to parseQueue                            │   │
│  └────────────┬─────────────────────────────────────────┘   │
│               ▼                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ ComputeWorker (application/workers/)                 │   │
│  │  ├── Reads from parseQueue                           │   │
│  │  ├── Checks cache (Cache service)                    │   │
│  │  ├── Generates plan (Compute service)                │   │
│  │  └── Writes to computeQueue                          │   │
│  └────────────┬─────────────────────────────────────────┘   │
│               ▼                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ CacheWorker (application/workers/)                   │   │
│  │  ├── Reads from computeQueue                         │   │
│  │  ├── Updates cache                                   │   │
│  │  └── Sends response to client                        │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

---

## Viktiga komponenter

| Komponent | Fil | Ansvar |
|-----------|-----|--------|
| **Server** | `server/Server.c` | Äger och koordinerar alla komponenter |
| **GridGuard** | `application/core/GridGuard.c` | Startar 4 worker threads och queues |
| **Queue** | `concurrency/sync/Queue.c` | Trådsäker FIFO med mutex/cond |
| **ThreadPool** | `concurrency/threads/ThreadPool.c` | Hanterar 20 workers × 50 klienter |
| **ClientHandler** | `server/ClientHandler.c` | Parsar kommandon, submittar requests |
| **Workers** | `application/workers/*` | Fetch → Parse → Compute → Cache |
| **Services** | `application/services/*` | Fetcher, Parser, Compute, Cache logic |

---

## Build System

### Makefile Targets

```bash
make              # Bygg server + klient
make server       # Bygg endast server
make client       # Bygg endast klient

make debug        # Debug build (-O0 -g -DDEBUG)
make release      # Release build (-O2 -DNDEBUG)
make profile      # Profiling build (-pg)
make coverage     # Coverage build (--coverage)

make test         # Kör alla tester
make test-api     # API fetch + parsing test
make test-logger  # Logger test
make test-pipeline # Multi-threaded pipeline test

make valgrind-server   # Memory leak check
make helgrind          # Thread safety check
make gprof-analyze     # Analyze profiling data

make clean        # Rensa build artifacts
make distclean    # Rensa allt (inkl logs)
```

### Compiler Flags

- **C:** `-Wall -Wextra -Werror -std=c11 -pthread -g`
- **C++:** `-Wall -Wextra -Werror -std=c++17 -pthread -g`
- **Linker:** `-pthread -lcurl`

---

## Konfiguration

### Config.h (src/application/config/Config.h)

```c
// Server
#define SERVER_PORT "8080"
#define SERVER_HOST "localhost"

// Thread Pool
#define MAX_THREADS 20
#define MAX_CLIENTS_PER_THREAD 50

// API URLs
#define WEATHER_API_BASE_URL "https://api.open-meteo.com/v1/forecast"
#define SPOTPRICE_API_BASE_URL "https://www.elprisetjustnu.se/api/v1/prices"

// Timeouts
#define SELECT_TIMEOUT_SEC 1
#define CLIENT_IDLE_TIMEOUT 300
#define HTTP_TIMEOUT 30
```

---

## Designprinciper

### 1. Separation of Concerns
Varje top-level mapp har ett tydligt ansvarsområde:
- **application/** - Vad systemet gör
- **infrastructure/** - Hur systemet körs
- **network/** - Hur systemet kommunicerar
- **concurrency/** - Hur systemet parallelliseras

### 2. Dependency Direction
Dependencies flödar inåt:
```
infrastructure/ ← application/ ← server/
network/       ← application/ ← server/
concurrency/   ← application/ ← server/
```

### 3. Testability
- Unit tests i `tests/unit/` - testar enskilda komponenter
- Integration tests i `tests/integration/` - testar systemet end-to-end

### 4. Extensibility
Tomma mappar (`daemon/`, `watchdog/`, `ipc/`) visar framtida expansionsområden.

---

## Framtida utökningar

### Daemon & Watchdog (infrastructure/)
```
src/infrastructure/
├── daemon/
│   ├── Daemon.c/h           # Daemonize GridGuard
│   └── PidFile.c/h          # PID file management
│
└── watchdog/
    ├── Watchdog.c/h         # Process monitoring
    └── main.c               # Watchdog entry point
```

### IPC (concurrency/ipc/)
```
src/concurrency/ipc/
├── Pipe.c/h                 # Named pipes (FIFO)
├── SharedMemory.c/h         # POSIX shared memory
└── Semaphore.c/h            # POSIX semaphores
```

### Protocols (network/protocol/)
```
src/network/protocol/
├── Protocol.h               # Protocol definitions
└── Serialization.c/h        # Data serialization
```

---

## Sammanfattning

GridGuard använder en väl genomtänkt mappstruktur som:
✅ Separerar concerns tydligt
✅ Skalerar väl med nya features
✅ Är lätt att navigera
✅ Följer industry best practices
✅ Stödjer både C och C++
✅ Är förberedd för daemon/watchdog implementation

Strukturen reflekterar systemets tre huvudlager:
1. **Application** - Business logic
2. **Infrastructure** - Platform services
3. **Network** - Communication

Med denna organisation är det enkelt att hitta kod, lägga till nya features och förstå systemets arkitektur.
