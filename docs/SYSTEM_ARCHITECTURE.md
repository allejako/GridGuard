# GridGuard System Architecture
**Complete System Design - From Current State to Production**

*Skapad: 2026-02-17*

---

## Innehållsförteckning

1. [Översikt](#översikt)
2. [Nuvarande Arkitektur](#nuvarande-arkitektur)
3. [Kursmål Mappning](#kursmål-mappning)
4. [Målarkitektur - Komplett System](#målarkitektur---komplett-system)
5. [Daemon & Watchdog Implementation](#daemon--watchdog-implementation)
6. [Database Layer (SQLite)](#database-layer-sqlite)
7. [Multi-User System](#multi-user-system)
8. [Next.js Platform](#nextjs-platform)
9. [C++ CLI Client](#c-cli-client)
10. [Deployment Architecture](#deployment-architecture)
11. [Development Roadmap](#development-roadmap)

---

## Översikt

GridGuard är ett multi-threaded energioptimeringssystem som kommer utvecklas från en enkel server-client applikation till ett fullständigt production-ready system med:

- **Daemon & Watchdog** - Processhantering och automatisk återstart
- **Multi-user support** - SQLite databas med 10,000+ användare
- **REST API** - För webbplatform integration
- **Next.js Platform** - Modern webbgränssnitt
- **C++ CLI Client** - Terminal-baserad klient
- **Authentication** - JWT + bcrypt
- **Shared Cache** - 15-minuters TTL för väder/priser

**Kursmål som täcks:**
- ✅ Vecka 1: fork(), exec(), wait() - Daemon/Watchdog
- ✅ Vecka 2-3: pthreads, mutex - GridGuard workers
- ✅ Vecka 4: Pipes - Heartbeat kommunikation
- ✅ Vecka 5: Signals, shared memory - Process management
- ✅ Vecka 6-9: C++ - CLI client, TCPClient
- ✅ Vecka 10-11: Profilering - Performance optimization

---

## Nuvarande Arkitektur

### System Overview (Nuläge)

```mermaid
flowchart TB
    subgraph CURRENT["GridGuard - Nuvarande System"]
        direction TB

        subgraph SERVER["Server Process (C)"]
            MAIN[main.c]
            SRV[Server.c]
            TCP[TCPServer.c]
            TPOOL[ThreadPool.c<br/>20 worker threads]
            CH[ClientHandler.c]
        end

        subgraph GRIDGUARD["GridGuard Core"]
            GG[GridGuard.c<br/>Orchestrator]

            subgraph WORKERS["Worker Threads"]
                FETCH[FetchWorker<br/>pthread]
                PARSE[ParseWorker<br/>pthread]
                COMPUTE[ComputeWorker<br/>pthread]
                CACHE_W[CacheWorker<br/>pthread]
            end

            subgraph QUEUES["Producer-Consumer Queues"]
                Q1[requestQueue]
                Q2[fetchQueue]
                Q3[parseQueue]
                Q4[computeQueue]
            end

            subgraph SERVICES["Services"]
                FETCHER[Fetcher.c<br/>curl HTTP]
                PARSER[Parser.c<br/>cJSON]
                COMP[Compute.c<br/>Energy calc]
                CACHE_S[Cache.c<br/>in-memory]
            end
        end

        subgraph INFRA["Infrastructure"]
            LOG[Logger.c<br/>Konsoll logs]
            SIG[SignalHandler.c<br/>SIGINT/SIGTERM]
        end

        CLIENT1[C++ Client<br/>TCPClient.cpp]
        CLIENT2[C++ Client<br/>TCPClient.cpp]

        MAIN --> SRV
        SRV --> TCP
        SRV --> TPOOL
        SRV --> GG

        TCP --> CLIENT1
        TCP --> CLIENT2

        TPOOL --> CH
        CH --> GG

        GG --> WORKERS
        WORKERS --> QUEUES
        QUEUES --> SERVICES

        FETCH --> FETCHER
        PARSE --> PARSER
        COMPUTE --> COMP
        CACHE_W --> CACHE_S

        SRV --> INFRA
    end

    EXT_API1[Open-Meteo API<br/>Weather data]
    EXT_API2[Elpriset.se API<br/>Spot prices]

    FETCHER -.->|HTTPS| EXT_API1
    FETCHER -.->|HTTPS| EXT_API2

    style CURRENT fill:#e3f2fd,stroke:#1976d2,stroke-width:3px,color:#000
    style GRIDGUARD fill:#fff3e0,stroke:#f57c00,stroke-width:2px,color:#000
    style WORKERS fill:#e8f5e9,stroke:#388e3c,stroke-width:2px,color:#000
    style SERVICES fill:#fce4ec,stroke:#c2185b,stroke-width:2px,color:#000
```

### Nuvarande Mappstruktur

```mermaid
graph TD
    ROOT[src/]

    ROOT --> APP[application/]
    ROOT --> INFRA[infrastructure/]
    ROOT --> NET[network/]
    ROOT --> CONC[concurrency/]
    ROOT --> SRV[server/]
    ROOT --> CLI[client/]
    ROOT --> TEST[tests/]
    ROOT --> LIBS[libs/]

    APP --> API[api/<br/>APIEndpoints.c]
    APP --> CONFIGS[configs/<br/>Config.h]
    APP --> CORE[core/<br/>GridGuard.c]
    APP --> MODELS[models/<br/>EnergyData.h]
    APP --> SERV[services/<br/>Fetcher, Parser, etc]
    APP --> WORK[workers/<br/>FetchWorker, etc]

    INFRA --> LOG_I[logging/<br/>Logger.c]
    INFRA --> SIG_I[signals/<br/>SignalHandler.c]
    INFRA --> DAEMON_I[daemon/<br/>❌ Tom]
    INFRA --> WATCH_I[watchdog/<br/>❌ Tom]

    NET --> NETSRV[server/<br/>TCPServer.c]
    NET --> NETCLI[client/<br/>TCPClient.cpp]

    CONC --> THREADS[threads/<br/>ThreadPool.c]
    CONC --> SYNC[sync/<br/>Queue.c, Scheduler.c]
    CONC --> IPC_C[ipc/<br/>❌ Tom]

    SRV --> SRVMAIN[main.c<br/>Server.c<br/>ClientHandler.c]

    CLI --> CLIMAIN[main.cpp<br/>C++ client]

    TEST --> UNIT[unit/<br/>test_logger.c]
    TEST --> INTEG[integration/<br/>test_api_fetch.c<br/>test_pipeline.c]

    LIBS --> CJSON[cJSON.c/h]

    style DAEMON_I fill:#ffebee,stroke:#c62828,stroke-width:2px,color:#000
    style WATCH_I fill:#ffebee,stroke:#c62828,stroke-width:2px,color:#000
    style IPC_C fill:#ffebee,stroke:#c62828,stroke-width:2px,color:#000
    style ROOT fill:#e1f5fe,stroke:#01579b,stroke-width:3px,color:#000
```

### Nuvarande Dataflöde

```mermaid
sequenceDiagram
    participant Client as C++ Client
    participant TCP as TCPServer
    participant Thread as ThreadPool Worker
    participant Handler as ClientHandler
    participant GG as GridGuard
    participant Fetch as FetchWorker
    participant Parse as ParseWorker
    participant Compute as ComputeWorker
    participant Cache as CacheWorker
    participant API as External APIs

    Client->>TCP: connect()
    TCP->>Thread: Assign to worker
    Thread->>Handler: Handle connection
    Client->>Handler: "forecast stockholm SE3"

    Handler->>GG: SubmitRequest(location, region)
    GG->>Fetch: Enqueue to requestQueue

    Fetch->>API: HTTP GET weather + prices
    API-->>Fetch: JSON responses
    Fetch->>Parse: Enqueue to fetchQueue

    Parse->>Parse: Parse JSON (cJSON)
    Parse->>Compute: Enqueue to parseQueue

    Compute->>Compute: Check in-memory cache
    Compute->>Compute: Generate energy plan
    Compute->>Cache: Enqueue to computeQueue

    Cache->>Cache: Update cache
    Cache->>Client: Send energy plan (TCP)

    Note over Client,Cache: ❌ Problem: location/region hårdkodat<br/>❌ Cache sparar färdiga planer<br/>❌ Ingen persistens
```

### Nuvarande Problem

```mermaid
mindmap
    root((Nuvarande<br/>Problem))
        Hårdkodat
            location = "stockholm"
            region = "SE3"
            SolarConfig hårdkodat
            BatteryConfig hårdkodat
            ConsumptionProfile hårdkodat

        Single User
            Alla users får samma data
            Ingen user isolation
            Ingen autentisering

        Cache
            Sparar EnergyData energiplaner
            User-specifikt kan inte delas
            Ingen persistens vid restart
            Försvinner vid krasch

        Processer
            Måste köra i terminal
            Ingen daemon-mode
            Ingen automatisk restart
            Saknar fork exec wait
            Saknar IPC pipes

        Platform
            Ingen webbplatform
            Bara TCP protocol
            Ingen REST API
            Ingen JWT auth
```

---

## Kursmål Mappning

### Vecka-för-vecka täckning

```mermaid
gantt
    title Kursmål Implementation i GridGuard
    dateFormat YYYY-MM-DD
    section Vecka 1: Processer
    fork(), exec(), wait()         :done, w1, 2026-02-18, 7d
    Daemon implementation          :done, w1a, 2026-02-18, 7d
    Watchdog process              :done, w1b, 2026-02-18, 7d

    section Vecka 2-3: Trådar
    GridGuard worker threads       :done, w2, 2026-01-01, 14d
    ThreadPool (20 workers)        :done, w2a, 2026-01-01, 14d
    Mutex & condition variables    :done, w2b, 2026-01-01, 14d
    Producer-consumer queues       :done, w2c, 2026-01-01, 14d

    section Vecka 4: Pipes
    Heartbeat pipe                :done, w4, 2026-02-18, 7d
    Watchdog ← → Daemon comm      :done, w4a, 2026-02-18, 7d

    section Vecka 5: IPC
    Signals (SIGTERM, SIGHUP)     :done, w5, 2026-01-01, 7d
    Shared memory (future)        :active, w5a, 2026-02-25, 7d
    Unix sockets (optional)       :active, w5b, 2026-02-25, 7d

    section Vecka 6-9: C++
    TCPClient.cpp                 :done, w6, 2026-01-01, 28d
    CLI Client                    :active, w6a, 2026-02-18, 14d
    RAII wrappers                 :active, w6b, 2026-02-25, 7d
    STL usage                     :active, w6c, 2026-02-25, 7d

    section Vecka 10-11: Optimering
    Cache optimization            :active, w10, 2026-03-01, 14d
    SQLite performance            :active, w10a, 2026-03-01, 14d
    Profiling med gprof           :active, w10b, 2026-03-08, 7d
```

### Kursmål → GridGuard Features

| Kursmål | Vecka | GridGuard Implementation | Status |
|---------|-------|--------------------------|--------|
| **fork(), exec(), wait()** | 1 | Watchdog startar Daemon med fork+exec, waitpid() monitoring | ✅ Planerat |
| **pthreads** | 2-3 | 4 worker threads (Fetch, Parse, Compute, Cache) | ✅ Implementerat |
| **mutex, cond** | 3 | Queue.c med pthread_mutex och pthread_cond | ✅ Implementerat |
| **Pipes** | 4 | Heartbeat pipe mellan Watchdog och Daemon | ✅ Planerat |
| **Signals** | 5 | SIGTERM för shutdown, SIGHUP för reload | ✅ Delvis |
| **Shared memory** | 5 | Optional: Cache i shared memory | 🔄 Future |
| **C++ klasser** | 6-7 | TCPClient.cpp, CLI client med OOP | ✅ TCPClient klar |
| **RAII** | 8 | Smart pointers, RAII wrappers för resurser | 🔄 Planerat |
| **STL** | 9 | std::vector, std::string, std::map i CLI | 🔄 Planerat |
| **Profilering** | 10 | gprof, valgrind på server | 🔄 Planerat |
| **Optimering** | 11 | Cache TTL, SQLite indexes, connection pooling | 🔄 Planerat |

---

## Målarkitektur - Komplett System

### High-Level System Architecture

```mermaid
flowchart TB
    subgraph USERS["End Users"]
        WEB_USER[Web User<br/>Browser]
        CLI_USER[CLI User<br/>Terminal]
        MOBILE[Mobile App<br/>Future]
    end

    subgraph FRONTEND["Frontend Layer"]
        NEXTJS[Next.js 15.1.6 Platform<br/>src/platform/]
        NEXTJS_PAGES[Pages<br/>Login, Dashboard, Config]
        NEXTJS_API[API Routes<br/>Server Actions]
        NEXTJS_COMP[Components<br/>React Components]

        NEXTJS --> NEXTJS_PAGES
        NEXTJS --> NEXTJS_API
        NEXTJS --> NEXTJS_COMP
    end

    subgraph API_LAYER["API Layer (New)"]
        REST_API[REST API Server<br/>src/infrastructure/api/<br/>libmicrohttpd]
        JWT_AUTH[JWT Authentication<br/>Middleware]
        ROUTER[Route Handler<br/>Router.c]

        REST_API --> JWT_AUTH
        JWT_AUTH --> ROUTER
    end

    subgraph BACKEND["Backend Layer"]
        subgraph PROCESS_MGMT["Process Management"]
            WATCHDOG[Watchdog Process<br/>fork + waitpid]
            DAEMON[GridGuard Daemon<br/>exec + setsid]
            HEARTBEAT[Heartbeat Pipe<br/>IPC]

            WATCHDOG -->|fork+exec| DAEMON
            DAEMON -.->|write| HEARTBEAT
            WATCHDOG -.->|read| HEARTBEAT
        end

        subgraph CORE_SERVER["Core Server"]
            TCP_SRV[TCPServer<br/>Port 8080]
            THREAD_POOL[ThreadPool<br/>20 workers]
            HANDLER[ClientHandler]
        end

        subgraph GRIDGUARD_CORE["GridGuard Core"]
            GG_ORCH[GridGuard.c<br/>Orchestrator]

            subgraph WORKERS["Worker Threads"]
                W1[FetchWorker]
                W2[ParseWorker]
                W3[ComputeWorker]
                W4[CacheWorker]
            end

            subgraph QUEUES["Queues"]
                Q1[requestQueue]
                Q2[fetchQueue]
                Q3[parseQueue]
                Q4[computeQueue]
            end
        end

        DAEMON --> CORE_SERVER
        CORE_SERVER --> GRIDGUARD_CORE
    end

    subgraph DATA_LAYER["Data Layer"]
        SQLITE[(SQLite DB<br/>gridguard.db)]

        subgraph DB_TABLES["Database Tables"]
            T1[users]
            T2[user_configs]
            T3[weather_cache]
            T4[price_cache]
            T5[request_log]
        end

        SQLITE --> DB_TABLES
    end

    subgraph EXTERNAL["External Services"]
        OPENMETEO[Open-Meteo API<br/>Weather]
        ELPRISET[Elpriset.se API<br/>Spot Prices]
    end

    CLI_CLIENT[C++ CLI Client<br/>gridguard-cli]

    WEB_USER --> NEXTJS
    CLI_USER --> CLI_CLIENT

    NEXTJS_API -->|HTTPS/REST| REST_API
    CLI_CLIENT -->|TCP| TCP_SRV

    ROUTER --> GG_ORCH
    HANDLER --> GG_ORCH

    WORKERS --> SQLITE
    W1 --> OPENMETEO
    W1 --> ELPRISET

    style USERS fill:#e8eaf6,stroke:#3f51b5,stroke-width:2px,color:#000
    style FRONTEND fill:#e1f5fe,stroke:#0277bd,stroke-width:2px,color:#000
    style API_LAYER fill:#fff3e0,stroke:#f57c00,stroke-width:2px,color:#000
    style BACKEND fill:#e8f5e9,stroke:#388e3c,stroke-width:2px,color:#000
    style DATA_LAYER fill:#fce4ec,stroke:#c2185b,stroke-width:2px,color:#000
    style EXTERNAL fill:#f3e5f5,stroke:#7b1fa2,stroke-width:2px,color:#000
```

### Complete Directory Structure (Målarkitektur)

```mermaid
graph TD
    ROOT[GridGuard/]

    ROOT --> SRC[src/]
    ROOT --> DOCS[docs/]
    ROOT --> SCRIPTS[scripts/]
    ROOT --> DATA[data/]
    ROOT --> LOGS[logs/]

    SRC --> PLATFORM[platform/<br/>⭐ NYT Next.js 15.1.6]
    SRC --> APP[application/]
    SRC --> INFRA[infrastructure/]
    SRC --> NET[network/]
    SRC --> CONC[concurrency/]
    SRC --> SRV[server/]
    SRC --> CLI_SRC[client/]
    SRC --> TEST[tests/]
    SRC --> LIBS[libs/]

    PLATFORM --> PL_APP[app/<br/>Next.js App Router]
    PLATFORM --> PL_COMP[components/<br/>React Components]
    PLATFORM --> PL_LIB[lib/<br/>API client, utils]
    PLATFORM --> PL_STYLES[styles/<br/>Tailwind CSS]
    PLATFORM --> PL_PUBLIC[public/<br/>Static assets]

    APP --> APP_API[api/]
    APP --> APP_CFG[configs/]
    APP --> APP_CORE[core/]
    APP --> APP_MOD[models/]
    APP --> APP_SERV[services/]
    APP --> APP_WORK[workers/]

    INFRA --> INFRA_API[api/<br/>⭐ NYT APIServer.c<br/>JWT.c, Auth.c]
    INFRA --> INFRA_DB[database/<br/>⭐ NYT Database.c<br/>UserDB.c, CacheDB.c]
    INFRA --> INFRA_DAEMON[daemon/<br/>⭐ NYT Daemon.c<br/>PidFile.c]
    INFRA --> INFRA_WATCH[watchdog/<br/>⭐ NYT Watchdog.c<br/>main.c]
    INFRA --> INFRA_LOG[logging/]
    INFRA --> INFRA_SIG[signals/]

    CONC --> CONC_THR[threads/]
    CONC --> CONC_SYNC[sync/]
    CONC --> CONC_IPC[ipc/<br/>⭐ NYT Pipe.c<br/>Heartbeat.c]

    DATA --> DATA_DB[gridguard.db<br/>⭐ NYT SQLite]
    DATA --> DATA_USERS[users/<br/>⭐ NYT JSON configs]

    style PLATFORM fill:#e1bee7,stroke:#6a1b9a,stroke-width:3px,color:#000
    style INFRA_API fill:#c5e1a5,stroke:#558b2f,stroke-width:2px,color:#000
    style INFRA_DB fill:#c5e1a5,stroke:#558b2f,stroke-width:2px,color:#000
    style INFRA_DAEMON fill:#c5e1a5,stroke:#558b2f,stroke-width:2px,color:#000
    style INFRA_WATCH fill:#c5e1a5,stroke:#558b2f,stroke-width:2px,color:#000
    style CONC_IPC fill:#c5e1a5,stroke:#558b2f,stroke-width:2px,color:#000
    style DATA fill:#ffccbc,stroke:#d84315,stroke-width:2px,color:#000
```

---

## Daemon & Watchdog Implementation

### Process Architecture

```mermaid
flowchart TB
    subgraph SYSTEMD["systemd (PID 1)"]
        INIT[System Init]
    end

    subgraph WATCHDOG_PROC["Watchdog Process (src/infrastructure/watchdog/)"]
        WD_MAIN[main.c<br/>Entry point]
        WD_LOOP[Main Loop<br/>Monitor daemon]
        WD_FORK[fork + exec<br/>Start daemon]
        WD_WAIT[waitpid<br/>Detect crash]
        WD_PIPE_READ[read heartbeat]
        WD_RESTART[Restart Logic<br/>Exponential backoff]

        WD_MAIN --> WD_LOOP
        WD_LOOP --> WD_PIPE_READ
        WD_PIPE_READ -->|No heartbeat| WD_RESTART
        WD_LOOP --> WD_WAIT
        WD_WAIT -->|Exit detected| WD_RESTART
        WD_RESTART --> WD_FORK
    end

    subgraph DAEMON_PROC["Daemon Process (src/infrastructure/daemon/)"]
        D_INIT[Daemon_Init<br/>Daemonize]
        D_SETSID[setsid<br/>Session leader]
        D_CHDIR[chdir /]
        D_UMASK[umask 0]
        D_CLOSE[Close std fds]
        D_PID[Write PID file]
        D_SERVER[Start Server]
        D_HEARTBEAT[Heartbeat Thread<br/>write to pipe]

        D_INIT --> D_SETSID
        D_SETSID --> D_CHDIR
        D_CHDIR --> D_UMASK
        D_UMASK --> D_CLOSE
        D_CLOSE --> D_PID
        D_PID --> D_SERVER
        D_SERVER --> D_HEARTBEAT
    end

    subgraph IPC["Inter-Process Communication"]
        PIPE[Heartbeat Pipe<br/>src/concurrency/ipc/Pipe.c]
        SIGNALS[Signals<br/>SIGTERM, SIGHUP, SIGUSR1]
        PIDFILE[PID File<br/>/var/run/gridguard.pid]
    end

    INIT -->|systemctl start| WD_MAIN
    WD_FORK -->|fork + exec| D_INIT

    D_HEARTBEAT -.->|write| PIPE
    WD_PIPE_READ -.->|read| PIPE

    WD_LOOP -.->|SIGTERM| SIGNALS
    SIGNALS -.->|signal| D_SERVER

    D_PID -.->|write| PIDFILE
    WD_LOOP -.->|read| PIDFILE

    style WATCHDOG_PROC fill:#bbdefb,stroke:#1976d2,stroke-width:3px,color:#000
    style DAEMON_PROC fill:#fff9c4,stroke:#f57f17,stroke-width:3px,color:#000
    style IPC fill:#c8e6c9,stroke:#388e3c,stroke-width:2px,color:#000
```

### Daemon Lifecycle

```mermaid
stateDiagram-v2
    [*] --> WatchdogStart: systemctl start

    WatchdogStart --> ForkDaemon: fork()
    ForkDaemon --> ExecDaemon: exec("gridguard-daemon")
    ExecDaemon --> DaemonInit: Process created

    DaemonInit --> Setsid: setsid()
    Setsid --> Chdir: chdir("/")
    Chdir --> Umask: umask(0)
    Umask --> CloseFDs: close(0,1,2)
    CloseFDs --> WritePID: write PID file
    WritePID --> StartServer: Server_Run()

    StartServer --> Running: Normal operation

    Running --> HeartbeatOK: Heartbeat sent
    HeartbeatOK --> Running: Continue

    Running --> Crashed: Segfault/Exception
    Running --> Stopped: SIGTERM received
    Running --> Reload: SIGHUP received

    Crashed --> WaitpidDetect: waitpid() returns
    WaitpidDetect --> RestartLogic: Check restart count
    RestartLogic --> ForkDaemon: Restart (< 5 times)
    RestartLogic --> AlertAdmin: Too many restarts

    Reload --> ReloadConfig: Read new config
    ReloadConfig --> Running: Continue

    Stopped --> Cleanup: Server_Shutdown()
    Cleanup --> [*]: Exit gracefully

    note right of Running
        Watchdog monitors:
        - Heartbeat every 5s
        - Process exit status
        - PID file validity
    end note
```

### Heartbeat Implementation

```mermaid
sequenceDiagram
    participant WD as Watchdog Process
    participant Pipe as Heartbeat Pipe
    participant HB as Heartbeat Thread
    participant Daemon as Daemon Process

    WD->>Pipe: pipe(fds) - Create pipe
    WD->>Daemon: fork() + exec()
    Daemon->>Daemon: Inherit pipe write fd
    Daemon->>HB: pthread_create(heartbeat_thread)

    loop Every 5 seconds
        HB->>Pipe: write("HEARTBEAT\n")
        Note over HB,Pipe: Timestamp + process stats
    end

    loop Monitor loop
        WD->>Pipe: read() with 10s timeout
        alt Heartbeat received
            Pipe-->>WD: "HEARTBEAT\n"
            WD->>WD: Reset timeout counter
        else Timeout (no heartbeat)
            Note over WD: 10s passed, no heartbeat
            WD->>WD: Increment timeout counter
            alt Counter > 3
                WD->>Daemon: kill(SIGTERM)
                WD->>WD: Wait for exit
                WD->>WD: Restart daemon
            end
        end
    end

    Note over WD,Daemon: Täcker Kursmål Vecka 4: Pipes<br/>Vecka 5: Signals
```

### Kod Exempel: Watchdog Main

```c
// src/infrastructure/watchdog/main.c
#include "Watchdog.h"

int main(int argc, char *argv[]) {
    Watchdog watchdog;

    // Initialize watchdog
    if (Watchdog_Init(&watchdog, "/usr/local/bin/gridguard-daemon") != 0) {
        fprintf(stderr, "Failed to initialize watchdog\n");
        return 1;
    }

    // Main monitoring loop
    while (watchdog.isRunning) {
        // Fork and exec daemon if needed
        if (Watchdog_StartDaemon(&watchdog) != 0) {
            sleep(watchdog.restartDelay);
            continue;
        }

        // Monitor daemon via heartbeat and waitpid
        WatchdogStatus status = Watchdog_Monitor(&watchdog);

        if (status == WATCHDOG_DAEMON_CRASHED) {
            LOG_ERROR("Daemon crashed! Restarting...");
            Watchdog_IncrementRestartCount(&watchdog);

            if (watchdog.restartCount > MAX_RESTART_ATTEMPTS) {
                LOG_FATAL("Too many restarts, giving up");
                break;
            }

            sleep(watchdog.restartDelay);
            watchdog.restartDelay *= 2; // Exponential backoff
        }
    }

    Watchdog_Shutdown(&watchdog);
    return 0;
}
```

---

## Database Layer (SQLite)

### Database Architecture

```mermaid
flowchart TB
    subgraph APPLICATION["Application Layer"]
        WORKERS[Worker Threads]
        SERVICES[Services]
    end

    subgraph DB_WRAPPER["Database Wrapper (src/infrastructure/database/)"]
        DB_MAIN[Database.c<br/>SQLite wrapper]
        USER_DB[UserDB.c<br/>User operations]
        CACHE_DB[CacheDB.c<br/>Cache operations]
        CONN_POOL[ConnectionPool.c<br/>10 connections]

        DB_MAIN --> USER_DB
        DB_MAIN --> CACHE_DB
        DB_MAIN --> CONN_POOL
    end

    subgraph SQLITE_DB["SQLite Database (data/gridguard.db)"]
        direction LR

        subgraph TABLES["Tables"]
            T1[users<br/>email, password_hash, api_key]
            T2[user_configs<br/>solar, battery, consumption]
            T3[weather_cache<br/>lat/lon → JSON, TTL]
            T4[price_cache<br/>region/date → JSON]
            T5[request_log<br/>analytics]
            T6[energy_plan_history<br/>optional]
            T7[token_blacklist<br/>JWT logout]
        end

        subgraph INDEXES["Indexes"]
            I1[idx_users_email]
            I2[idx_users_api_key]
            I3[idx_weather_expires]
            I4[idx_price_date]
        end
    end

    WORKERS --> DB_WRAPPER
    SERVICES --> DB_WRAPPER

    DB_WRAPPER --> SQLITE_DB

    style DB_WRAPPER fill:#ffe0b2,stroke:#e65100,stroke-width:2px,color:#000
    style SQLITE_DB fill:#f3e5f5,stroke:#7b1fa2,stroke-width:2px,color:#000
    style TABLES fill:#e1bee7,stroke:#6a1b9a,stroke-width:1px,color:#000
    style INDEXES fill:#c5cae9,stroke:#3f51b5,stroke-width:1px,color:#000
```

### Database Schema

```mermaid
erDiagram
    USERS ||--o{ USER_CONFIGS : has
    USERS ||--o{ REQUEST_LOG : generates
    USERS ||--o{ ENERGY_PLAN_HISTORY : stores

    USERS {
        text user_id PK
        text email UK
        text password_hash
        text api_key UK
        text first_name
        text last_name
        int created_at
        int updated_at
        int last_login_at
        bool active
        bool email_verified
        text reset_token
        int reset_token_expires
    }

    USER_CONFIGS {
        text user_id PK
        text location_name
        real latitude
        real longitude
        text region
        text timezone
        real solar_panel_efficiency
        real solar_panel_area_m2
        real solar_orientation_degrees
        real solar_tilt_degrees
        real solar_peak_power_kw
        real battery_capacity_kwh
        real battery_max_charge_rate_kw
        real battery_max_discharge_rate_kw
        real battery_min_soc_percent
        real battery_max_soc_percent
        real battery_current_soc_percent
        real battery_efficiency
        real consumption_base_load_kw
        real consumption_peak_load_kw
        real consumption_avg_daily_kwh
        int updated_at
    }

    WEATHER_CACHE {
        text location_key PK
        text location_name
        text forecast_json
        int fetched_at
        int expires_at
    }

    PRICE_CACHE {
        text region PK
        text date PK
        text prices_json
        int fetched_at
    }

    REQUEST_LOG {
        int id PK
        text user_id FK
        text location
        text region
        int timestamp
        int processing_time_ms
        bool cache_hit_weather
        bool cache_hit_price
    }

    ENERGY_PLAN_HISTORY {
        int id PK
        text user_id FK
        int generated_at
        text plan_json
        real total_cost_sek
        real total_grid_import_kwh
    }

    TOKEN_BLACKLIST {
        text token_hash PK
        int expires_at
    }
```

### Cache Strategy (Fixed Architecture)

```mermaid
flowchart TB
    subgraph REQUEST["User Request"]
        USER1[User A<br/>Stockholm, 20m² solar]
        USER2[User B<br/>Stockholm, 30m² solar]
        USER3[User C<br/>Göteborg, 25m² solar]
    end

    subgraph FETCH_STAGE["FetchWorker (Shared Cache)"]
        FETCH[FetchWorker Thread]

        subgraph CACHE_CHECK["Cache Check"]
            CHECK_WEATHER[Check weather_cache<br/>location_key='59.33,18.07']
            CHECK_PRICE[Check price_cache<br/>region='SE3', date='2026-02-17']
        end

        FETCH --> CHECK_WEATHER
        FETCH --> CHECK_PRICE
    end

    subgraph CACHE_DB["SQLite Cache Tables"]
        WEATHER_TBL[weather_cache<br/>TTL: 15 min]
        PRICE_TBL[price_cache<br/>TTL: 15 min]
    end

    subgraph COMPUTE_STAGE["ComputeWorker (User-Specific)"]
        COMPUTE[ComputeWorker Thread]
        CALC[Calculate with<br/>user config]
    end

    subgraph EXTERNAL["External APIs"]
        OPENMETEO[Open-Meteo]
        ELPRISET[Elpriset.se]
    end

    USER1 --> FETCH
    USER2 --> FETCH
    USER3 --> FETCH

    CHECK_WEATHER -.->|HIT| WEATHER_TBL
    CHECK_PRICE -.->|HIT| PRICE_TBL

    CHECK_WEATHER -.->|MISS| OPENMETEO
    CHECK_PRICE -.->|MISS| ELPRISET

    OPENMETEO -.->|Save| WEATHER_TBL
    ELPRISET -.->|Save| PRICE_TBL

    FETCH --> COMPUTE
    COMPUTE --> CALC

    CALC -->|User A config| RESULT1[Plan A: 20m²]
    CALC -->|User B config| RESULT2[Plan B: 30m²]
    CALC -->|User C config| RESULT3[Plan C: Göteborg]

    style FETCH_STAGE fill:#c5e1a5,stroke:#558b2f,stroke-width:2px,color:#000
    style COMPUTE_STAGE fill:#ffccbc,stroke:#d84315,stroke-width:2px,color:#000
    style CACHE_DB fill:#b39ddb,stroke:#5e35b1,stroke-width:2px,color:#000
```

### Connection Pooling

```mermaid
sequenceDiagram
    participant W1 as Worker Thread 1
    participant W2 as Worker Thread 2
    participant Pool as Connection Pool
    participant DB1 as SQLite Connection 1
    participant DB2 as SQLite Connection 2
    participant File as gridguard.db

    W1->>Pool: GetConnection()
    Pool->>DB1: Allocate conn 1
    Pool-->>W1: Return conn 1

    W2->>Pool: GetConnection()
    Pool->>DB2: Allocate conn 2
    Pool-->>W2: Return conn 2

    W1->>DB1: SELECT user_config WHERE user_id=?
    DB1->>File: Query
    File-->>DB1: Result
    DB1-->>W1: UserConfig

    W1->>Pool: ReleaseConnection(conn 1)
    Pool->>DB1: Mark available

    W2->>DB2: SELECT weather_cache WHERE location_key=?
    DB2->>File: Query
    File-->>DB2: Result
    DB2-->>W2: WeatherData

    W2->>Pool: ReleaseConnection(conn 2)
    Pool->>DB2: Mark available

    Note over Pool: Pool size: 10 connections<br/>Reused across workers<br/>Thread-safe with mutex
```

---

## Multi-User System

### User Journey: Registration → Forecast

```mermaid
sequenceDiagram
    participant User as Web User
    participant Next as Next.js Platform
    participant API as REST API Server
    participant DB as SQLite Database
    participant GG as GridGuard Core
    participant Ext as External APIs

    Note over User,DB: 1. Registration
    User->>Next: Fill registration form
    Next->>API: POST /api/v1/users/register<br/>{email, password}
    API->>API: Validate email format
    API->>API: Hash password (bcrypt)
    API->>API: Generate userId, apiKey
    API->>DB: INSERT INTO users
    API->>DB: INSERT INTO user_configs (defaults)
    API-->>Next: {userId, apiKey, JWT}
    Next->>Next: Store JWT in localStorage
    Next-->>User: Redirect to dashboard

    Note over User,DB: 2. Login
    User->>Next: Enter email + password
    Next->>API: POST /api/v1/users/login
    API->>DB: SELECT * FROM users WHERE email=?
    DB-->>API: User record
    API->>API: Verify password (bcrypt)
    API->>API: Generate JWT (24h expiry)
    API->>DB: UPDATE last_login_at
    API-->>Next: {JWT, user info}
    Next->>Next: Store JWT
    Next-->>User: Dashboard

    Note over User,GG: 3. Configure System
    User->>Next: Fill config form<br/>(solar, battery, consumption)
    Next->>API: PUT /api/v1/users/me/config<br/>Authorization: Bearer JWT
    API->>API: Verify JWT → Extract userId
    API->>DB: UPDATE user_configs<br/>WHERE user_id=?
    API-->>Next: Success
    Next-->>User: Config saved

    Note over User,Ext: 4. Get Forecast
    User->>Next: Click "Get Forecast"
    Next->>API: GET /api/v1/forecast<br/>Authorization: Bearer JWT
    API->>API: Verify JWT → userId
    API->>DB: SELECT user_configs WHERE user_id=?
    DB-->>API: User config (solar, battery, etc)
    API->>GG: SubmitRequest(userId, config)
    GG->>GG: Check cache (weather/prices)
    alt Cache HIT
        GG->>GG: Use cached weather/prices
    else Cache MISS
        GG->>Ext: Fetch weather + prices
        Ext-->>GG: JSON responses
        GG->>DB: Store in cache (15 min TTL)
    end
    GG->>GG: Compute with user config
    GG-->>API: Personalized energy plan
    API-->>Next: Forecast data
    Next-->>User: Display graph + summary
```

### WorkRequest Evolution

```mermaid
flowchart LR
    subgraph BEFORE["Före (Hårdkodat)"]
        WR1[WorkRequest]
        WR1_FD[clientFd: 5]
        WR1_LOC[location: 'stockholm']
        WR1_REG[region: 'SE3']

        WR1 --> WR1_FD
        WR1 --> WR1_LOC
        WR1 --> WR1_REG
    end

    subgraph AFTER["Efter (User-Specific)"]
        WR2[WorkRequest]
        WR2_FD[clientFd: 5]
        WR2_UID[userId: 'user_abc123']
        WR2_CFG[UserConfig]

        WR2 --> WR2_FD
        WR2 --> WR2_UID
        WR2 --> WR2_CFG

        WR2_CFG --> CFG_LOC[location: 'Stockholm']
        WR2_CFG --> CFG_LAT[latitude: 59.33]
        WR2_CFG --> CFG_LON[longitude: 18.07]
        WR2_CFG --> CFG_REG[region: 'SE3']
        WR2_CFG --> CFG_SOL[SolarConfig]
        WR2_CFG --> CFG_BAT[BatteryConfig]
        WR2_CFG --> CFG_CONS[ConsumptionProfile]

        CFG_SOL --> SOL_1[panelAreaM2: 30.0]
        CFG_SOL --> SOL_2[efficiency: 0.20]
        CFG_SOL --> SOL_3[orientation: 180°]

        CFG_BAT --> BAT_1[capacityKwh: 15.0]
        CFG_BAT --> BAT_2[maxChargeRateKw: 7.5]

        CFG_CONS --> CONS_1[baseLoadKw: 1.0]
        CFG_CONS --> CONS_2[avgDailyKwh: 25.0]
    end

    style BEFORE fill:#ffebee,stroke:#c62828,stroke-width:2px,color:#000
    style AFTER fill:#e8f5e9,stroke:#2e7d32,stroke-width:2px,color:#000
```

### Cache Sharing Example

```mermaid
flowchart TB
    subgraph TIMELINE["Timeline: 14:00 - 14:20"]
        T1[14:00:00<br/>User A request]
        T2[14:05:00<br/>User B request]
        T3[14:10:00<br/>User C request]
        T4[14:15:01<br/>User D request]
        T5[14:20:00<br/>User E request]
    end

    subgraph CACHE_OPS["Cache Operations"]
        OP1[Weather Cache MISS<br/>Fetch from API<br/>Store with TTL=15min<br/>expires_at = 14:15:00]
        OP2[Weather Cache HIT<br/>Age: 5 min<br/>Use cached data]
        OP3[Weather Cache HIT<br/>Age: 10 min<br/>Use cached data]
        OP4[Weather Cache EXPIRED<br/>Fetch new data<br/>Store expires_at = 14:30:00]
        OP5[Weather Cache HIT<br/>Age: 5 min<br/>New cache entry]
    end

    subgraph RESULTS["Compute Results"]
        R1[Plan for User A<br/>20m² solar, 10kWh battery]
        R2[Plan for User B<br/>30m² solar, 15kWh battery]
        R3[Plan for User C<br/>25m² solar, 12kWh battery]
        R4[Plan for User D<br/>20m² solar, 8kWh battery]
        R5[Plan for User E<br/>35m² solar, 20kWh battery]
    end

    T1 --> OP1 --> R1
    T2 --> OP2 --> R2
    T3 --> OP3 --> R3
    T4 --> OP4 --> R4
    T5 --> OP5 --> R5

    style OP1 fill:#ffccbc,stroke:#d84315,stroke-width:2px,color:#000
    style OP2 fill:#c5e1a5,stroke:#558b2f,stroke-width:2px,color:#000
    style OP3 fill:#c5e1a5,stroke:#558b2f,stroke-width:2px,color:#000
    style OP4 fill:#ffccbc,stroke:#d84315,stroke-width:2px,color:#000
    style OP5 fill:#c5e1a5,stroke:#558b2f,stroke-width:2px,color:#000
```

---

## Next.js Platform

### Platform Architecture (src/platform/)

```mermaid
flowchart TB
    subgraph NEXTJS["Next.js 15.1.6 Platform"]
        direction TB

        subgraph APP_ROUTER["app/ (App Router)"]
            ROOT_LAYOUT[layout.tsx<br/>Root layout]
            ROOT_PAGE[page.tsx<br/>Landing page]

            AUTH_GROUP["(auth)/"]
            AUTH_LOGIN[login/page.tsx]
            AUTH_REGISTER[register/page.tsx]

            DASH_GROUP["(dashboard)/"]
            DASH_LAYOUT[layout.tsx<br/>Dashboard layout]
            DASH_HOME[page.tsx<br/>Overview]
            DASH_CONFIG[config/page.tsx<br/>Settings]
            DASH_FORECAST[forecast/page.tsx<br/>Energy plan]
            DASH_HISTORY[history/page.tsx<br/>Historical data]

            API_ROUTES[api/]
            API_AUTH[auth/nextauth/route.ts]
            API_FORECAST[forecast/route.ts]
            API_CONFIG[config/route.ts]
        end

        subgraph COMPONENTS["components/"]
            COMP_UI[ui/<br/>Button, Card, Input]
            COMP_DASH[dashboard/<br/>ForecastChart, ConfigForm]
            COMP_AUTH[auth/<br/>LoginForm, RegisterForm]
        end

        subgraph LIB["lib/"]
            LIB_API[api-client.ts<br/>Fetch wrapper]
            LIB_AUTH[auth.ts<br/>NextAuth config]
            LIB_UTILS[utils.ts<br/>Helpers]
            LIB_TYPES[types.ts<br/>TypeScript types]
        end

        subgraph STYLES["styles/"]
            GLOBAL[globals.css<br/>Tailwind base]
            THEME[theme.css<br/>Custom theme]
        end

        ROOT_LAYOUT --> AUTH_GROUP
        ROOT_LAYOUT --> DASH_GROUP
        ROOT_LAYOUT --> API_ROUTES

        AUTH_GROUP --> AUTH_LOGIN
        AUTH_GROUP --> AUTH_REGISTER

        DASH_LAYOUT --> DASH_HOME
        DASH_LAYOUT --> DASH_CONFIG
        DASH_LAYOUT --> DASH_FORECAST
        DASH_LAYOUT --> DASH_HISTORY

        COMPONENTS --> COMP_UI
        COMPONENTS --> COMP_DASH
        COMPONENTS --> COMP_AUTH

        LIB --> LIB_API
        LIB --> LIB_AUTH
        LIB --> LIB_UTILS
    end

    subgraph BACKEND_API["Backend REST API"]
        REST[APIServer.c<br/>Port 3001]
    end

    API_ROUTES -.->|Proxy| REST
    LIB_API -.->|HTTPS| REST

    style NEXTJS fill:#e1bee7,stroke:#6a1b9a,stroke-width:3px,color:#000
    style APP_ROUTER fill:#f3e5f5,stroke:#7b1fa2,stroke-width:2px,color:#000
    style COMPONENTS fill:#e1f5fe,stroke:#0277bd,stroke-width:2px,color:#000
    style LIB fill:#fff3e0,stroke:#f57c00,stroke-width:2px,color:#000
```

### Page Hierarchy

```mermaid
graph TD
    ROOT[Landing Page]

    ROOT --> AUTH[Auth Pages]
    ROOT --> DASH[Dashboard Pages]

    AUTH --> LOGIN[Login Form]
    AUTH --> REGISTER[Registration Form]
    AUTH --> RESET[Password Reset]

    DASH --> OVERVIEW[Overview and Stats]
    DASH --> CONFIG[System Configuration]
    DASH --> FORECAST[Energy Forecast]
    DASH --> HISTORY[Historical Data]
    DASH --> PROFILE[User Profile]

    CONFIG --> CONFIG_SOLAR[Solar config section]
    CONFIG --> CONFIG_BATTERY[Battery config section]
    CONFIG --> CONFIG_CONSUMPTION[Consumption config section]
    CONFIG --> CONFIG_LOCATION[Location config section]

    FORECAST --> FORECAST_CHART[Forecast chart component]
    FORECAST --> FORECAST_SUMMARY[Summary stats]
    FORECAST --> FORECAST_ACTIONS[Recommended actions]

    style ROOT fill:#e8eaf6,stroke:#3f51b5,stroke-width:3px,color:#000
    style DASH fill:#e1f5fe,stroke:#0277bd,stroke-width:2px,color:#000
    style FORECAST fill:#e8f5e9,stroke:#388e3c,stroke-width:2px,color:#000
```

### Component Structure

```mermaid
flowchart TB
    subgraph PAGES["Page Components"]
        FORECAST_PAGE[ForecastPage]
        CONFIG_PAGE[ConfigPage]
    end

    subgraph DASHBOARD_COMPONENTS["Dashboard Components"]
        FORECAST_CHART[ForecastChart<br/>Recharts visualization]
        ENERGY_SUMMARY[EnergySummary<br/>Cost, savings, etc]
        ACTION_CARDS[ActionCards<br/>Recommendations]

        CONFIG_FORM[ConfigForm<br/>Multi-step form]
        SOLAR_SECTION[SolarConfigSection]
        BATTERY_SECTION[BatteryConfigSection]
        CONSUMPTION_SECTION[ConsumptionConfigSection]
    end

    subgraph UI_COMPONENTS["UI Components (shadcn/ui)"]
        BUTTON[Button]
        CARD[Card]
        INPUT[Input]
        FORM[Form]
        CHART[Chart]
        DIALOG[Dialog]
        TOAST[Toast]
    end

    FORECAST_PAGE --> FORECAST_CHART
    FORECAST_PAGE --> ENERGY_SUMMARY
    FORECAST_PAGE --> ACTION_CARDS

    CONFIG_PAGE --> CONFIG_FORM
    CONFIG_FORM --> SOLAR_SECTION
    CONFIG_FORM --> BATTERY_SECTION
    CONFIG_FORM --> CONSUMPTION_SECTION

    FORECAST_CHART --> CHART
    ENERGY_SUMMARY --> CARD
    ACTION_CARDS --> CARD

    CONFIG_FORM --> FORM
    CONFIG_FORM --> INPUT
    CONFIG_FORM --> BUTTON

    style PAGES fill:#e1bee7,stroke:#6a1b9a,stroke-width:2px,color:#000
    style DASHBOARD_COMPONENTS fill:#c5e1a5,stroke:#558b2f,stroke-width:2px,color:#000
    style UI_COMPONENTS fill:#b2dfdb,stroke:#00695c,stroke-width:2px,color:#000
```

### API Client Flow

```mermaid
sequenceDiagram
    participant Page as ForecastPage.tsx
    participant Client as api-client.ts
    participant Next as Next.js API Route
    participant Backend as C Backend API
    participant DB as SQLite DB

    Page->>Client: getForecast()
    Client->>Client: Get JWT from localStorage

    alt Option 1: Direct to Backend
        Client->>Backend: GET /api/v1/forecast<br/>Authorization: Bearer JWT
        Backend->>Backend: Verify JWT
        Backend->>DB: Load user config
        Backend->>Backend: Generate forecast
        Backend-->>Client: Forecast data
    else Option 2: Via Next.js Proxy
        Client->>Next: GET /api/forecast
        Next->>Next: Get session
        Next->>Backend: GET /api/v1/forecast<br/>With JWT
        Backend->>DB: Load user config
        Backend-->>Next: Forecast data
        Next-->>Client: Proxied response
    end

    Client-->>Page: Forecast data
    Page->>Page: Render chart
```

---

## C++ CLI Client

### CLI Architecture

```mermaid
flowchart TB
    subgraph CLI_APP["C++ CLI Client (src/client/)"]
        MAIN[main.cpp<br/>Entry point]
        CLI[CLI.cpp<br/>Command processor]
        AUTH[Auth.cpp<br/>Login/register]
        CONFIG_CLI[ConfigManager.cpp<br/>Local config]
        API[APIClient.cpp<br/>Backend communication]
        UI[UI.cpp<br/>Terminal UI]

        MAIN --> CLI
        CLI --> AUTH
        CLI --> CONFIG_CLI
        CLI --> API
        CLI --> UI
    end

    subgraph NETWORK["Network Layer"]
        TCP_CLIENT[TCPClient.cpp<br/>Existing]
        HTTP_CLIENT[HTTPClient.cpp<br/>New for REST API]
    end

    subgraph BACKEND["Backend Options"]
        TCP_SERVER[TCP Server<br/>Port 8080]
        REST_API[REST API<br/>Port 3001]
    end

    subgraph STORAGE["Local Storage"]
        CONFIG_FILE[~/.gridguard/config.json<br/>User preferences]
        TOKEN_FILE[~/.gridguard/token<br/>JWT token]
    end

    API --> TCP_CLIENT
    API --> HTTP_CLIENT

    TCP_CLIENT --> TCP_SERVER
    HTTP_CLIENT --> REST_API

    CONFIG_CLI --> CONFIG_FILE
    AUTH --> TOKEN_FILE

    style CLI_APP fill:#e1f5fe,stroke:#0277bd,stroke-width:3px,color:#000
    style NETWORK fill:#fff3e0,stroke:#f57c00,stroke-width:2px,color:#000
    style STORAGE fill:#f3e5f5,stroke:#7b1fa2,stroke-width:2px,color:#000
```

### CLI Commands

```mermaid
mindmap
    root((gridguard-cli))
        Auth Commands
            login
                gridguard-cli login
            register
                gridguard-cli register
            logout
                gridguard-cli logout

        Config Commands
            config view
                gridguard-cli config view
            config set
                gridguard-cli config set solar.area 30
            config import
                gridguard-cli config import config.json

        Forecast Commands
            forecast get
                gridguard-cli forecast
            forecast today
                gridguard-cli forecast --today
            forecast week
                gridguard-cli forecast --week

        History Commands
            history list
                gridguard-cli history
            history export
                gridguard-cli history export data.csv

        System Commands
            status
                gridguard-cli status
            version
                gridguard-cli version
            help
                gridguard-cli help
```

### CLI Session Example

```mermaid
sequenceDiagram
    participant User as Terminal User
    participant CLI as CLI Client
    participant Auth as Auth Module
    participant API as API Client
    participant Backend as Backend API

    User->>CLI: gridguard-cli login
    CLI->>Auth: Prompt for email/password
    User->>Auth: Enter credentials
    Auth->>API: POST /api/v1/users/login
    API->>Backend: Login request
    Backend-->>API: {JWT, user info}
    API-->>Auth: Token received
    Auth->>Auth: Save to ~/.gridguard/token
    Auth-->>CLI: Login successful
    CLI-->>User: Welcome, John!

    User->>CLI: gridguard-cli config set solar.area 30
    CLI->>API: Read JWT from file
    CLI->>API: PUT /api/v1/users/me/config
    API->>Backend: Update config (with JWT)
    Backend-->>API: Success
    API-->>CLI: Config updated
    CLI-->>User: Solar panel area set to 30 m²

    User->>CLI: gridguard-cli forecast
    CLI->>API: GET /api/v1/forecast (with JWT)
    API->>Backend: Request forecast
    Backend-->>API: Energy plan data
    API-->>CLI: Forecast received
    CLI->>CLI: Render ASCII chart
    CLI-->>User: Display forecast table + chart
```

### CLI Output Example

```
$ gridguard-cli forecast

╔══════════════════════════════════════════════════════════════════╗
║                    GridGuard Energy Forecast                      ║
║                      Stockholm, SE3                               ║
║                    2026-02-17 15:00 - 18:00                      ║
╚══════════════════════════════════════════════════════════════════╝

Time     │ Action          │ Solar │ Battery │ Grid  │ Price │ Cost
─────────┼─────────────────┼───────┼─────────┼───────┼───────┼──────
15:00    │ CHARGE_BATTERY  │ 2.3   │ +1.1    │ 0.0   │ 1.01  │ 0.00
16:00    │ CHARGE_BATTERY  │ 1.8   │ +0.8    │ 0.0   │ 1.07  │ 0.00
17:00    │ DISCHARGE_BAT   │ 0.5   │ -1.2    │ 0.0   │ 1.36  │ 0.00
18:00    │ DISCHARGE_BAT   │ 0.0   │ -1.5    │ 0.0   │ 1.35  │ 0.00

Summary:
  Total Cost:        0.00 SEK (saved 15.30 SEK)
  Grid Import:       0.0 kWh
  Grid Export:       0.0 kWh
  Battery Cycles:    0.3
  Solar Production:  4.6 kWh
  Consumption:       5.2 kWh

Recommendations:
  ✓ Charge battery during low prices (15:00-16:00)
  ✓ Use battery during peak prices (17:00-19:00)
  ✓ Avoid grid import until 22:00 (high prices)
```

---

## Deployment Architecture

### Production Deployment

```mermaid
flowchart TB
    subgraph INTERNET["Internet"]
        USERS[End Users]
    end

    subgraph CLOUDFLARE["Cloudflare"]
        CDN[CDN + SSL]
        WAF[Web Application Firewall]
    end

    subgraph VPS["VPS Server (Ubuntu 22.04)"]
        subgraph NGINX["Nginx Reverse Proxy"]
            NGINX_443[Port 443 HTTPS]
            NGINX_ROUTES[Route Configuration]
        end

        subgraph SERVICES["System Services (systemd)"]
            WATCHDOG_SVC[gridguard-watchdog.service]
            NEXT_SVC[gridguard-platform.service<br/>Next.js on port 3000]
            API_SVC[gridguard-api.service<br/>REST API on port 3001]
        end

        subgraph PROCESSES["Running Processes"]
            WD_PROC[Watchdog Process]
            DAEMON_PROC[GridGuard Daemon]
            NEXT_PROC[Next.js Server]
            API_PROC[API Server]
        end

        subgraph DATA["Data Storage"]
            SQLITE[SQLite DB<br/>/var/lib/gridguard/gridguard.db]
            LOGS_DIR[Logs<br/>/var/log/gridguard/]
            BACKUPS[Backups<br/>/var/backups/gridguard/]
        end
    end

    USERS --> CDN
    CDN --> WAF
    WAF --> NGINX_443

    NGINX_ROUTES --> NEXT_SVC
    NGINX_ROUTES --> API_SVC

    WATCHDOG_SVC --> WD_PROC
    WD_PROC --> DAEMON_PROC
    NEXT_SVC --> NEXT_PROC
    API_SVC --> API_PROC

    DAEMON_PROC --> SQLITE
    API_PROC --> SQLITE

    DAEMON_PROC --> LOGS_DIR
    API_PROC --> LOGS_DIR

    SQLITE -.->|Daily backup| BACKUPS

    style VPS fill:#e8f5e9,stroke:#2e7d32,stroke-width:3px,color:#000
    style SERVICES fill:#fff3e0,stroke:#f57c00,stroke-width:2px,color:#000
    style DATA fill:#f3e5f5,stroke:#7b1fa2,stroke-width:2px,color:#000
```

### Nginx Configuration

```nginx
# /etc/nginx/sites-available/gridguard

upstream nextjs {
    server localhost:3000;
}

upstream api {
    server localhost:3001;
}

server {
    listen 443 ssl http2;
    server_name gridguard.se www.gridguard.se;

    ssl_certificate /etc/letsencrypt/live/gridguard.se/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/gridguard.se/privkey.pem;

    # Next.js Platform
    location / {
        proxy_pass http://nextjs;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection 'upgrade';
        proxy_set_header Host $host;
        proxy_cache_bypass $http_upgrade;
    }

    # REST API
    location /api/ {
        proxy_pass http://api;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    }
}
```

### Systemd Services

```mermaid
flowchart TB
    subgraph SYSTEMD["systemd Service Manager"]
        MULTI_USER[multi-user.target]
    end

    subgraph SERVICES["GridGuard Services"]
        WD_SVC[watchdog service]
        NEXT_SVC[platform service]
        API_SVC[api service]
    end

    subgraph DEPS["Dependencies"]
        NETWORK[network.target]
        SQLITE_CHECK[sqlite3 installed]
    end

    MULTI_USER --> NETWORK
    NETWORK --> WD_SVC
    NETWORK --> NEXT_SVC
    NETWORK --> API_SVC

    WD_SVC -.->|Requires| SQLITE_CHECK
    NEXT_SVC -.->|After| WD_SVC
    API_SVC -.->|After| WD_SVC

    WD_SVC --> PROC_WD[watchdog binary]
    NEXT_SVC --> PROC_NEXT[npm start]
    API_SVC --> PROC_API[api binary]

    style SYSTEMD fill:#e3f2fd,stroke:#1976d2,stroke-width:2px,color:#000
    style SERVICES fill:#fff3e0,stroke:#f57c00,stroke-width:2px,color:#000
```

---

## Development Roadmap

### Implementation Phases

```mermaid
gantt
    title GridGuard Development Roadmap
    dateFormat YYYY-MM-DD

    section Phase 1: Multi-User Foundation
    UserConfig struct + JSON loader       :p1a, 2026-02-18, 2d
    Update WorkRequest with userId        :p1b, after p1a, 2d
    ClientHandler with userId             :p1c, after p1b, 1d

    section Phase 2: Database Layer
    SQLite schema design                  :p2a, 2026-02-18, 1d
    Database wrapper implementation       :p2b, after p2a, 3d
    UserDB + CacheDB                      :p2c, after p2b, 2d
    Migrate from JSON to SQLite           :p2d, after p2c, 1d

    section Phase 3: Cache Refactoring
    Move cache to FetchWorker             :p3a, after p2d, 2d
    SQLite cache integration              :p3b, after p3a, 2d
    Test cache hit rates                  :p3c, after p3b, 1d

    section Phase 4: Daemon & Watchdog
    Daemon implementation                 :p4a, 2026-02-25, 3d
    Watchdog process                      :p4b, after p4a, 3d
    Heartbeat pipe (IPC)                  :p4c, after p4a, 2d
    systemd service files                 :p4d, after p4b, 1d

    section Phase 5: REST API & Auth
    APIServer with libmicrohttpd          :p5a, 2026-03-01, 3d
    JWT implementation                    :p5b, after p5a, 2d
    bcrypt password hashing               :p5c, after p5a, 1d
    User registration endpoint            :p5d, after p5b, 2d
    User login endpoint                   :p5e, after p5d, 1d
    Config endpoints                      :p5f, after p5e, 2d
    Forecast endpoint                     :p5g, after p5f, 2d

    section Phase 6: Next.js Platform
    Project setup (Next.js 15.1.6)        :p6a, 2026-03-08, 1d
    Authentication pages                  :p6b, after p6a, 2d
    Dashboard layout                      :p6c, after p6b, 2d
    Config form                           :p6d, after p6c, 3d
    Forecast visualization                :p6e, after p6d, 3d
    API integration                       :p6f, after p6a, 5d

    section Phase 7: C++ CLI Client
    CLI framework setup                   :p7a, 2026-03-15, 2d
    Auth commands                         :p7b, after p7a, 2d
    Config commands                       :p7c, after p7b, 2d
    Forecast commands                     :p7d, after p7c, 2d
    HTTPClient implementation             :p7e, after p7a, 3d

    section Phase 8: Testing & Deployment
    Integration tests                     :p8a, 2026-03-22, 3d
    Performance testing                   :p8b, after p8a, 2d
    Security audit                        :p8c, after p8b, 2d
    VPS setup                             :p8d, 2026-03-27, 2d
    Nginx configuration                   :p8e, after p8d, 1d
    SSL certificates                      :p8f, after p8e, 1d
    Production deployment                 :p8g, after p8f, 1d
```

### Feature Completion Status

```mermaid
pie title Feature Implementation Status
    "✅ Completed (GridGuard core, ThreadPool, Queue)" : 35
    "🔄 In Progress (Documentation, Planning)" : 15
    "📋 Planned (Daemon, Watchdog, SQLite)" : 30
    "🚀 Future (Next.js, CLI, Deployment)" : 20
```

---

## Sammanfattning

### System Evolution

```mermaid
flowchart LR
    V1[Version 1.0<br/>Current<br/>Single-threaded<br/>Hardcoded config<br/>In-memory cache]

    V2[Version 2.0<br/>Multi-user<br/>SQLite database<br/>User configs<br/>Shared cache]

    V3[Version 3.0<br/>Daemon Watchdog<br/>Process mgmt<br/>Auto-restart<br/>IPC pipes]

    V4[Version 4.0<br/>REST API<br/>JWT auth<br/>Next.js platform<br/>C++ CLI client]

    V5[Version 5.0<br/>Production<br/>10k users<br/>Cloud deploy<br/>Monitoring]

    V1 -->|Phase 1-3| V2
    V2 -->|Phase 4| V3
    V3 -->|Phase 5-7| V4
    V4 -->|Phase 8| V5

    style V1 fill:#ffebee,stroke:#c62828,stroke-width:2px,color:#000
    style V2 fill:#fff3e0,stroke:#e65100,stroke-width:2px,color:#000
    style V3 fill:#e1f5fe,stroke:#0277bd,stroke-width:2px,color:#000
    style V4 fill:#f3e5f5,stroke:#7b1fa2,stroke-width:2px,color:#000
    style V5 fill:#e8f5e9,stroke:#2e7d32,stroke-width:3px,color:#000
```

### Kursmål Coverage

| Område | Kursvecka | GridGuard Implementation | Files |
|--------|-----------|--------------------------|-------|
| **Processer** | 1 | Daemon + Watchdog med fork/exec/wait | `infrastructure/daemon/`, `infrastructure/watchdog/` |
| **Trådar** | 2-3 | Worker threads, ThreadPool, Queue | `application/workers/`, `concurrency/threads/` |
| **IPC Pipes** | 4 | Heartbeat pipe mellan processer | `concurrency/ipc/Pipe.c` |
| **IPC Advanced** | 5 | Signals, shared memory (optional) | `infrastructure/signals/` |
| **C++ Basics** | 6 | TCPClient, CLI client | `network/client/`, `client/` |
| **C++ OOP** | 7 | Klasser, RAII wrappers | CLI client implementation |
| **RAII** | 8 | Smart pointers, resource management | C++ client modules |
| **STL** | 9 | vector, map, string i CLI | C++ client data structures |
| **Profilering** | 10 | gprof, valgrind | Performance testing |
| **Optimering** | 11 | Cache TTL, SQLite indexes | Database optimization |

---

**Status:** Detta dokument är den kompletta arkitekturreferensen för GridGuard.

**Nästa steg:** Börja med Phase 1 (Multi-User Foundation).

**Total estimated tid:** ~8-10 veckor för komplett implementation.
