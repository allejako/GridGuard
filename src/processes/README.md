# GridGuard Processes

This directory contains separate process implementations for the hybrid IPC architecture.

## Process Architecture

GridGuard uses a **multi-process architecture** with the following independent processes:

```
Main Process (GridGuard-server)
├── HTTP Server + ThreadPool
├── Compute Worker Thread
└── Spawns child processes:
    ├── Fetcher Process
    └── Parser Process
```

## Processes

### 1. Fetcher Process (`fetcher/`)

**Binary:** `bin/GridGuard-fetcher`
**Spawned by:** Main process via `fork() + execl()`
**Communication:**
- **IN:** Reads `WorkRequest` from stdin (anonymous pipe from main)
- **OUT:** Writes `FetchResult` to FIFO (named pipe to parser)

**Responsibilities:**
- HTTP API calls to Open-Meteo (weather forecasts)
- HTTP API calls to Elpriset.se (spot prices)
- Caching in POSIX shared memory (`weatherCache`, `priceCache`)

**Files:**
- `main.c` - Entry point, signal handling, process initialization
- `fetcher.c` - Core fetch logic, cache management
- `fetcher.h` - Public interface

---

### 2. Parser Process (`parser/`)

**Binary:** `bin/GridGuard-parser`
**Spawned by:** Main process via `fork() + execl()`
**Communication:**
- **IN:** Reads `FetchResult` from FIFO (named pipe from fetcher)
- **OUT:** Serves `ParseResult` via Unix domain socket to Compute thread

**Responsibilities:**
- Parse JSON from Open-Meteo API
- Parse JSON from Elpriset.se API
- Build unified `ForecastData` structure
- Unix socket server (accepts connections from Compute)

**Files:**
- `main.c` - Entry point, signal handling, process initialization
- `parser.c` - Core parsing logic, socket server
- `parser.h` - Public interface

---

## IPC Flow

```
HTTP Request
    ↓
[Main: HTTP Thread]
    ↓ (writes WorkRequest to pipe)
[Fetcher Process]
    ↓ (reads from stdin, writes to FIFO)
[Parser Process]
    ↓ (reads from FIFO, listens on Unix socket)
[Main: Compute Thread]
    ↓ (connects to socket, reads ParseResult)
[Main: HTTP Thread]
    ↓ (receives response via WorkCompletion)
HTTP Response
```

## Process Lifecycle

### Startup
1. Main process calls `GridGuard_Initiate()`
2. Creates anonymous pipe (HTTP → Fetch)
3. Creates FIFO (Fetch → Parse)
4. Forks Fetcher → `execl("bin/GridGuard-fetcher")`
5. Forks Parser → `execl("bin/GridGuard-parser")`
6. Starts Compute worker thread (Unix socket client)

### Shutdown
1. Main sends `SIGTERM` to Fetcher and Parser
2. Calls `waitpid()` to reap child processes
3. Cleans up IPC resources (pipes, FIFO, socket, shm)

---

## Course Concepts Demonstrated

This architecture demonstrates all IPC mechanisms from **Kursvecka 1-5**:

| Week | Concept | Implementation |
|------|---------|----------------|
| 1 | `fork()`, `exec()`, `waitpid()` | Process spawning in `GridGuard.c` |
| 2 | `pthread_create()`, `pthread_join()` | Compute thread |
| 3 | `pthread_mutex_t`, `pthread_cond_t` | `WorkCompletion` synchronization |
| 4 | Anonymous pipes (`pipe()`, `dup2()`) | HTTP → Fetch |
| 4 | Named pipes (`mkfifo()`) | Fetch → Parse FIFO |
| 5 | Unix domain sockets | Parse → Compute |
| 5 | POSIX shared memory (`shm_open`, `mmap`) | Weather/price caches |
| 5 | POSIX semaphores (`sem_open`) | Cache synchronization |

---

## Building

```bash
make             # Build all processes
make clean       # Clean build artifacts
```

Binaries output:
- `bin/GridGuard-server` (main process)
- `bin/GridGuard-fetcher` (spawned via exec)
- `bin/GridGuard-parser` (spawned via exec)

---

**Note:** Each process is a standalone executable with its own `main()` function. They communicate exclusively through IPC mechanisms (pipes, FIFOs, sockets, shared memory) - no shared global state.
