# GridGuard System Arkitektur - Visualisering

**Datum:** 2026-03-03
**Syfte:** Komplett visualisering av GridGuards systemarkitektur med Mermaid-diagram

---

## 1. Systemöversikt - Multi-Process Arkitektur

```mermaid
graph TB
    subgraph "Watchdog Process"
        WD[Watchdog<br/>PID: varies]
    end

    subgraph "Server Process (Main)"
        MAIN[main.c<br/>PID: varies]
        SRV[Server<br/>Port 8080]
        TP[ThreadPool<br/>20 workers]
        GG[GridGuard Core]
        DB[(SQLite<br/>Database)]

        MAIN --> SRV
        SRV --> TP
        SRV --> GG
        GG --> DB
    end

    subgraph "Fetcher Process"
        FETCH[Fetcher<br/>fork/exec]
        API1[Open-Meteo API]
        API2[Elpriset.se API]
        CACHE1[SharedCache<br/>Weather]

        FETCH --> API1
        FETCH --> API2
        FETCH --> CACHE1
    end

    subgraph "Parser Process"
        PARSE[Parser<br/>fork/exec]
        JSON[cJSON Parser]
        CACHE2[SharedCache<br/>Prices]
        USOCK[Unix Socket<br/>Server]

        PARSE --> JSON
        PARSE --> CACHE2
        PARSE --> USOCK
    end

    subgraph "Compute Worker Thread"
        COMP[Compute Thread]
        ALG[Energy Algorithm<br/>NOCT Model]

        COMP --> ALG
    end

    WD -.supervises.-> MAIN
    WD -.heartbeat pipe.-> MAIN

    GG -->|fork and exec| FETCH
    GG -->|fork and exec| PARSE
    GG -->|pthread_create| COMP

    GG -->|anonymous pipe| FETCH
    FETCH -->|named FIFO| PARSE
    PARSE -->|Unix socket| COMP

    CACHE1 -.shared memory.-> CACHE2

    TP -.HTTP workers.-> COMP

    style WD fill:#ff9999
    style MAIN fill:#99ccff
    style FETCH fill:#99ff99
    style PARSE fill:#ffff99
    style COMP fill:#ff99ff
```

---

## 2. Startup Sequence - Process Lifecycle

```mermaid
sequenceDiagram
    autonumber
    participant User
    participant Watchdog
    participant Main as Server Main
    participant GG as GridGuard Core
    participant Fetcher
    participant Parser
    participant Compute as Compute Thread
    participant DB as SQLite

    User->>Watchdog: ./bin/GridGuard-watchdog
    activate Watchdog

    Note over Watchdog: Creates pipe for heartbeat
    Watchdog->>Main: fork and exec ./bin/GridGuard-server
    activate Main

    Note over Main: Detects GRIDGUARD_HEARTBEAT_FD<br/>→ Daemonizes
    Main->>Main: Daemon_Init()
    Main->>Watchdog: Start heartbeat thread

    Main->>Main: Logger_Initiate("logs/server.log")
    Main->>Main: Server_Initiate()

    Main->>GG: GridGuard_Initiate()
    activate GG

    GG->>DB: Database_Initiate(gridguard.db)
    activate DB
    DB-->>GG: OK

    GG->>GG: Compute_Initiate()
    GG->>GG: SharedCache_Create("/gridguard_weather")
    GG->>GG: SharedCache_Create("/gridguard_price")

    Note over GG: Create FIFO for Fetch→Parse
    GG->>GG: mkfifo("/tmp/gridguard_fetch_to_parse.fifo")

    Note over GG: Create anonymous pipe for HTTP→Fetch
    GG->>GG: pipe(requestPipe)

    GG->>Fetcher: fork()
    activate Fetcher
    Note over Fetcher: Child process
    Fetcher->>Fetcher: dup2(requestPipe[0], STDIN)
    Fetcher->>Fetcher: execl("GridGuard-fetcher", fifoPath)
    Note over Fetcher: Now running fetcher main()

    Note over GG: Parent process, sleep 1s
    GG->>GG: sleep(1)

    GG->>Parser: fork()
    activate Parser
    Note over Parser: Child process
    Parser->>Parser: execl("GridGuard-parser", fifoPath, socketPath)
    Note over Parser: Now running parser main()

    Note over GG: Parent process, sleep 1s
    GG->>GG: sleep(1)

    GG->>Compute: pthread_create(ComputeWorkerHybrid_Run)
    activate Compute
    Note over Compute: Connects to Parser via Unix socket

    GG-->>Main: GridGuard initialized
    deactivate GG

    Main->>Main: ThreadPool_Initiate(20 workers)
    Main->>Main: TCPServer_Initiate(port 8080)

    Main-->>Watchdog: Heartbeat OK

    Note over Main,Compute: System ready, waiting for requests

    Main->>Main: Server_Run() - main loop

    loop Every 5 seconds
        Main->>Watchdog: write(heartbeat_fd, "H")
        Watchdog->>Watchdog: Check heartbeat
    end
```

---

## 3. Request Flow - Complete Pipeline

```mermaid
sequenceDiagram
    autonumber
    participant Client
    participant TCP as TCP Socket
    participant Worker as HTTP Worker Thread
    participant Handler as ClientHandler
    participant JWT as JWT Validator
    participant DB as SQLite
    participant GG as GridGuard Core
    participant Reg as Completion Registry
    participant WC as WorkCompletion
    participant Fetcher
    participant Cache as SharedCache
    participant API as External APIs
    participant Parser
    participant Compute as Compute Thread

    Client->>TCP: HTTP/1.1 GET /forecast<br/>Authorization: Bearer {JWT}
    activate TCP

    TCP->>Worker: accept() → clientSocket
    activate Worker

    Worker->>Handler: Client_HandleRequest(fd, app)
    activate Handler

    Handler->>Handler: HTTPRequest_Parse(fd)
    Note over Handler: Parse method, path, headers, body

    Handler->>JWT: JWT_Validate(token, &claims)
    activate JWT
    JWT->>JWT: Base64 decode header/payload
    JWT->>JWT: HMAC-SHA256 verify signature
    JWT->>JWT: Check expiry (exp < now?)
    JWT-->>Handler: OK → claims.subject = "user123"
    deactivate JWT

    Handler->>DB: UserConfigDB_Get("user123", &cfg)
    activate DB
    DB-->>Handler: UserConfig{lat, lon, solar...}
    deactivate DB

    Handler->>WC: WorkCompletion_Initiate(&wc)
    activate WC
    WC->>WC: pthread_mutex_init(&mutex)
    WC->>WC: pthread_cond_init(&cond)
    WC->>WC: isReady = false

    Handler->>Reg: RegisterCompletion("user123", &wc)
    activate Reg
    Note over Reg: Store in global registry<br/>for Compute thread lookup

    Handler->>GG: GridGuard_SubmitRequest(&req, &wc)
    activate GG

    GG->>GG: pthread_mutex_lock(&mutex)
    GG->>Fetcher: write(requestPipeFd, &req)
    Note over GG,Fetcher: Anonymous pipe IPC
    GG->>GG: pthread_mutex_unlock(&mutex)
    GG-->>Handler: Request submitted
    deactivate GG

    Note over Worker: Worker blocks here
    Handler->>WC: WorkCompletion_Wait(&wc)
    WC->>WC: pthread_mutex_lock(&mutex)
    WC->>WC: while (!isReady)<br/>pthread_cond_wait(&cond, &mutex)

    Fetcher->>Fetcher: read(STDIN, &req)
    Note over Fetcher: Wakes up from blocking read

    Fetcher->>Cache: SharedCache_Lookup("/gridguard_weather", key)
    activate Cache
    Cache-->>Fetcher: MISS (or expired)
    deactivate Cache

    Fetcher->>API: HTTPClient_Get(Open-Meteo API)
    activate API
    API-->>Fetcher: Weather JSON (96 hours)
    deactivate API

    Fetcher->>API: HTTPClient_Get(Elpriset.se API)
    activate API
    API-->>Fetcher: Price JSON (24-96 hours)
    deactivate API

    Fetcher->>Cache: SharedCache_Store("weather", json)
    activate Cache
    Cache->>Cache: sem_wait()
    Cache->>Cache: Store data and timestamp
    Cache->>Cache: sem_post()
    deactivate Cache

    Fetcher->>Parser: write(fifoFd, &fetchResult)
    Note over Fetcher,Parser: Named FIFO IPC

    Parser->>Parser: read(fifoFd, &fetchResult)
    Note over Parser: Wakes up from blocking read

    Parser->>Parser: cJSON_Parse(weatherJson)
    Parser->>Parser: cJSON_Parse(priceJson)
    Parser->>Parser: Merge into ForecastData

    Parser->>DB: UserConfigDB_Get("user123", &cfg)
    activate DB
    DB-->>Parser: UserConfig
    deactivate DB

    Parser->>Parser: Build ParseResult{userId, config, forecast}

    Parser->>Compute: accept() on Unix socket
    Note over Parser,Compute: Unix domain socket IPC
    Parser->>Compute: write(clientSocket, &parseResult)

    Compute->>Compute: read(socket, &parseResult)
    Note over Compute: Wakes up from blocking read

    Compute->>Reg: FindCompletionByUserId("user123")
    Reg-->>Compute: &wc

    Compute->>Compute: Compute_GenerateEnergyPlan(...)
    Note over Compute: NOCT solar model<br/>Percentile threshold<br/>BUY/SELL/IDLE decisions

    Compute->>Compute: serialize_energy_plan(&plan, json)

    Compute->>Reg: UnregisterCompletion("user123")
    deactivate Reg

    Compute->>WC: WorkCompletion_Signal(&wc, json)
    WC->>WC: pthread_mutex_lock(&mutex)
    WC->>WC: strcpy(buffer, json)
    WC->>WC: isReady = true
    WC->>WC: pthread_cond_signal(&cond)
    Note over WC: Wakes up Worker thread!
    WC->>WC: pthread_mutex_unlock(&mutex)

    WC-->>Handler: Wake up from wait
    WC->>WC: Read json from buffer
    deactivate WC

    Handler->>TCP: HTTPResponse_SendJson(fd, json)
    deactivate Handler

    Worker->>Worker: close(fd)
    deactivate Worker

    TCP-->>Client: HTTP/1.1 200 OK<br/>Content-Type: application/json<br/>{...forecast...}
    deactivate TCP
```

---

## 4. IPC Mechanisms - Detailed View

```mermaid
graph LR
    subgraph "HTTP Worker Thread"
        H1[Thread 1]
        H2[Thread 2]
        H3[Thread N]
    end

    subgraph "GridGuard Core"
        PIPE[Anonymous Pipe<br/>int array]
        MUTEX[pthread_mutex_t]
    end

    subgraph "Fetcher Process"
        F_STDIN[STDIN<br/>dup2 from pipe]
        F_FIFO[FIFO Write FD]
    end

    subgraph "Named FIFO"
        FIFO[/tmp/gridguard_fetch_to_parse.fifo<br/>mkfifo mode 0666]
    end

    subgraph "Parser Process"
        P_FIFO[FIFO Read FD]
        P_SOCK[Unix Socket Server<br/>bind and listen]
    end

    subgraph "Unix Domain Socket"
        SOCK[/tmp/gridguard_parse_to_compute.sock<br/>AF_UNIX SOCK_STREAM]
    end

    subgraph "Compute Thread"
        C_SOCK[Unix Socket Client<br/>connect]
    end

    subgraph "POSIX Shared Memory"
        SHM_W[/gridguard_weather<br/>shm_open and mmap]
        SHM_P[/gridguard_price<br/>shm_open and mmap]
        SEM_W[Semaphore<br/>/gridguard_weather_sem]
        SEM_P[Semaphore<br/>/gridguard_price_sem]
    end

    H1 -->|write WorkRequest| MUTEX
    H2 -->|write WorkRequest| MUTEX
    H3 -->|write WorkRequest| MUTEX
    MUTEX -->|serialize writes| PIPE

    PIPE -->|read end| F_STDIN

    F_STDIN -.WorkRequest.-> F_FIFO
    F_FIFO -->|write FetchResult| FIFO

    FIFO -->|read FetchResult| P_FIFO

    P_FIFO -.ParseResult.-> P_SOCK
    P_SOCK <-->|read/write| SOCK
    SOCK <-->|read/write| C_SOCK

    F_FIFO -.cache.-> SHM_W
    F_FIFO -.cache.-> SHM_P
    SHM_W -.protect.-> SEM_W
    SHM_P -.protect.-> SEM_P

    P_FIFO -.read cache.-> SHM_W
    P_FIFO -.read cache.-> SHM_P

    style PIPE fill:#ffcccc
    style FIFO fill:#ccffcc
    style SOCK fill:#ccccff
    style SHM_W fill:#ffffcc
    style SHM_P fill:#ffffcc
```

---

## 5. Thread Synchronization - WorkCompletion Pattern

```mermaid
stateDiagram-v2
    [*] --> Created: WorkCompletion_Initiate()

    Created --> Registered: RegisterCompletion(userId, &wc)

    Registered --> Waiting: WorkCompletion_Wait()
    note right of Waiting
        pthread_mutex_lock()
        while (!isReady)
            pthread_cond_wait(&cond, &mutex)
    end note

    Waiting --> Processing: Compute thread working...
    note right of Processing
        Parallel execution:
        - HTTP worker: blocked on cond_wait
        - Compute thread: running algorithm
    end note

    Processing --> Signaled: WorkCompletion_Signal(json)
    note right of Signaled
        pthread_mutex_lock()
        strcpy(buffer, json)
        isReady = true
        pthread_cond_signal(&cond)
        pthread_mutex_unlock()
    end note

    Signaled --> Completed: HTTP worker wakes up
    note right of Completed
        pthread_cond_wait returns
        Read json from buffer
        pthread_mutex_unlock()
    end note

    Completed --> Destroyed: WorkCompletion_Destroy()

    Destroyed --> [*]

    Waiting --> TimedOut: Timeout (30s)
    note right of TimedOut
        If Compute thread crashes
        or pipeline hangs
    end note

    TimedOut --> Error
    Error --> [*]
```

---

## 6. Compute Algorithm - Energy Planning

```mermaid
flowchart TD
    START([Compute_GenerateEnergyPlan])

    INPUT[/"Input:<br/>ForecastData 96 hours<br/>UserConfig solar and grid fees"/]

    START --> INPUT

    INPUT --> PASS1[Pass 1: Cost Calculation]

    PASS1 --> LOOP1{For each hour i<br/>in forecast}

    LOOP1 -->|valid| CALC_COST["Calculate total cost:<br/>hour = localtime timestamp hour<br/>gridFee = GetGridFee hour<br/>cost = spot + gridFee + tax × VAT"]

    CALC_COST --> STORE["Store cost in arrays:<br/>entryCosts at index i<br/>sortedCosts increment count"]

    STORE --> LOOP1

    LOOP1 -->|done| SORT[qsort sortedCosts]

    SORT --> PASS2[Pass 2: Threshold Calculation]

    PASS2 --> PERCENTILE["p30idx = sortCount × 0.30<br/>buyThreshold = sortedCosts[p30idx]<br/>medianCost = sortedCosts[p50idx]"]

    PERCENTILE --> PASS3[Pass 3: Per-Hour Decisions]

    PASS3 --> LOOP2{For each hour i}

    LOOP2 -->|valid| SOLAR[Calculate Solar Production]

    SOLAR --> NOCT["NOCT temperature model:<br/>Calculate cell temperature<br/>Apply temperature coefficient<br/>Compute solar production kWh"]

    NOCT --> NET["netKwh = production - consumption"]

    NET --> DECISION{Decision Tree}

    DECISION -->|netKwh > 0.05| CHECK_SPOT{spotPrice >= 0?}
    CHECK_SPOT -->|Yes| SELL[action = SELL_TO_GRID<br/>totalExport add netKwh]
    CHECK_SPOT -->|No| IDLE1[action = IDLE<br/>negative price, don't export]

    DECISION -->|netKwh <= 0.05| CHECK_COST{cost <= buyThreshold?}
    CHECK_COST -->|Yes| BUY[action = BUY_FROM_GRID]
    CHECK_COST -->|No| IDLE2[action = IDLE]

    SELL --> TRACK
    IDLE1 --> TRACK
    BUY --> TRACK
    IDLE2 --> TRACK

    TRACK["Track totals:<br/>if netKwh negative:<br/>  increment totalImport<br/>  increment totalCost"]

    TRACK --> FILL["Fill EnergyDataEntry:<br/>timestamp, action, production,<br/>consumption, spotPrice, totalCost,<br/>savingsVsMedian"]

    FILL --> LOOP2

    LOOP2 -->|done| OUTPUT[/"Output:<br/>EnergyData plan<br/>96 entries with summary"/]

    OUTPUT --> END([Return 0])

    style START fill:#90EE90
    style PASS1 fill:#FFE4B5
    style PASS2 fill:#FFE4B5
    style PASS3 fill:#FFE4B5
    style SELL fill:#98FB98
    style BUY fill:#87CEEB
    style IDLE1 fill:#DDA0DD
    style IDLE2 fill:#DDA0DD
    style END fill:#90EE90
```

---

## 7. Database Schema

```mermaid
erDiagram
    user_configs ||--o{ schedules : "has many"

    user_configs {
        TEXT user_id PK
        TEXT location
        REAL latitude
        REAL longitude
        TEXT region
        REAL solar_area_m2
        REAL solar_efficiency
        REAL consumption_kwh
        REAL grid_fee_low
        REAL grid_fee_normal
        REAL grid_fee_high
        INTEGER updated_at
    }

    schedules {
        TEXT schedule_id PK
        TEXT user_id FK
        TEXT load_id
        INTEGER scheduled_start
        INTEGER duration_minutes
        REAL power_kw
        REAL estimated_cost_sek
        REAL savings_sek
        TEXT status
        INTEGER created_at
    }
```

**Example Data:**

```sql
-- user_configs
INSERT INTO user_configs VALUES (
    'user123',
    'Stockholm',
    59.3293,
    18.0686,
    'SE3',
    20.0,      -- 20 m² solar panels
    0.18,      -- 18% efficiency
    0.5,       -- 0.5 kWh/h base load
    0.25,      -- night grid fee
    0.35,      -- day grid fee
    0.45,      -- peak grid fee
    1709481600 -- 2024-03-03 12:00:00
);

-- schedules
INSERT INTO schedules VALUES (
    'user123_1709481600',
    'user123',
    'ev_charging',
    1709596800,  -- 2024-03-05 00:00:00 (scheduled start)
    480,         -- 8 hours
    5.0,         -- 5 kW
    78.44,       -- estimated cost
    50.76,       -- savings vs charging now
    'pending',
    1709481600   -- created at
);
```

---

## 8. API Endpoints - REST Interface

```mermaid
graph TB
    subgraph "Public Endpoints (No Auth)"
        HEALTH[GET /health]
    end

    subgraph "User Configuration (Requires JWT)"
        GET_CFG[GET /user/config]
        PUT_CFG[PUT /user/config]
    end

    subgraph "Energy Forecast (Requires JWT)"
        GET_FC[GET /forecast]
    end

    subgraph "Load Scheduling (Requires JWT)"
        GET_SCH[GET /schedule]
        POST_SCH[POST /schedule]
        DEL_SCH[DELETE /schedule/:id]
    end

    CLIENT[HTTP Client]

    CLIENT -->|No auth| HEALTH
    CLIENT -->|Bearer JWT| GET_CFG
    CLIENT -->|Bearer JWT| PUT_CFG
    CLIENT -->|Bearer JWT| GET_FC
    CLIENT -->|Bearer JWT| GET_SCH
    CLIENT -->|Bearer JWT| POST_SCH
    CLIENT -->|Bearer JWT| DEL_SCH

    HEALTH -.->|200 OK| RES1["status: ok"]

    GET_CFG -.->|200 OK| RES2["location, solar config"]
    GET_CFG -.->|404| RES3["error: No config found"]

    PUT_CFG -.->|200 OK| RES4["status: ok"]
    PUT_CFG -.->|400| RES5["error: Invalid coordinates"]

    GET_FC -.->|200 OK| RES6["user_id, forecast array"]
    GET_FC -.->|400| RES7["error: User config not set"]

    POST_SCH -.->|200 OK| RES8["schedule_id, savings"]
    POST_SCH -.->|400| RES9["error: No valid window"]

    GET_SCH -.->|200 OK| RES10["schedules array"]

    DEL_SCH -.->|200 OK| RES11["status: cancelled"]
    DEL_SCH -.->|404| RES12["error: Schedule not found"]

    style HEALTH fill:#90EE90
    style GET_CFG fill:#87CEEB
    style PUT_CFG fill:#FFB6C1
    style GET_FC fill:#DDA0DD
    style POST_SCH fill:#F0E68C
    style GET_SCH fill:#F0E68C
    style DEL_SCH fill:#FFA07A
```

---

## 9. Data Structures - Core Models

```mermaid
classDiagram
    class ForecastEntry {
        +time_t timestamp
        +double solarIrradiance
        +double cloudCover
        +double temperature
        +double windSpeed
        +double humidity
        +double spotPriceSek
        +bool valid
    }

    class ForecastData {
        +ForecastEntry entries[96]
        +int count
        +time_t lastUpdated
    }

    class EnergyAction {
        <<enumeration>>
        ACTION_BUY_FROM_GRID
        ACTION_SELL_TO_GRID
        ACTION_IDLE
    }

    class EnergyDataEntry {
        +time_t timestamp
        +EnergyAction action
        +double productionKwh
        +double consumptionKwh
        +double spotPrice
        +double totalCostSek
        +double savingsVsMedianSek
        +bool valid
    }

    class EnergyData {
        +EnergyDataEntry entries[96]
        +int count
        +time_t generatedAt
        +double totalCostSek
        +double totalGridImportKwh
        +double totalGridExportKwh
    }

    class UserConfig {
        +char userId[128]
        +char location[64]
        +double latitude
        +double longitude
        +char region[16]
        +double solarAreaM2
        +double solarEfficiency
        +double consumptionKwh
        +double gridFee_low
        +double gridFee_normal
        +double gridFee_high
        +long updatedAt
    }

    class WorkRequest {
        +char userId[64]
        +char lat[16]
        +char lon[16]
        +char region[16]
        +char location[64]
        +double solarAreaM2
        +double solarEfficiency
        +double consumptionKwh
        +double gridFee_low
        +double gridFee_normal
        +double gridFee_high
    }

    class ScheduleWindow {
        +time_t scheduledStart
        +int durationMinutes
        +double powerKw
        +double estimatedCostSek
        +double savingsSek
    }

    ForecastData *-- ForecastEntry
    EnergyData *-- EnergyDataEntry
    EnergyDataEntry --> EnergyAction
    WorkRequest ..> UserConfig : created from

    note for ForecastData "Input to Compute algorithm"
    note for EnergyData "Output from Compute algorithm"
    note for WorkRequest "IPC message: HTTP → Fetcher"
```

---

## 10. Deployment Architecture

```mermaid
C4Deployment
    title Deployment Diagram - GridGuard Production

    Deployment_Node(server, "Linux Server", "Ubuntu 22.04 LTS") {
        Deployment_Node(docker, "Docker Container (optional)", "Docker") {
            Container(watchdog, "Watchdog", "C Process", "Supervises server")
            Container(gridguard, "GridGuard Server", "C Process", "Main application")
            Container(fetcher, "Fetcher", "C Process", "API client")
            Container(parser, "Parser", "C Process", "JSON parser")

            ContainerDb(sqlite, "SQLite", "Database", "Stores user configs & schedules")
            ContainerDb(shm, "POSIX Shared Memory", "/dev/shm", "Caches weather & prices")
        }

        Deployment_Node(logs, "Log Directory", "./logs/") {
            Container(log_server, "server.log", "Text file")
            Container(log_fetcher, "fetcher.log", "Text file")
            Container(log_parser, "parser.log", "Text file")
            Container(log_watchdog, "watchdog.log", "Text file")
        }
    }

    Deployment_Node(client, "Client Device", "Any HTTP client") {
        Container(browser, "Web Browser", "JavaScript", "React/Vue/etc")
        Container(mobile, "Mobile App", "Swift/Kotlin", "iOS/Android")
        Container(curl, "CLI Tool", "curl/httpie", "Testing")
    }

    Deployment_Node(apis, "External APIs", "Internet") {
        Container(openmeteo, "Open-Meteo", "REST API", "Weather forecast")
        Container(elpriset, "Elpriset.se", "REST API", "Electricity prices")
    }

    Rel(watchdog, gridguard, "supervises", "pipe and signals")
    Rel(gridguard, fetcher, "spawns", "fork and exec")
    Rel(gridguard, parser, "spawns", "fork and exec")
    Rel(gridguard, sqlite, "reads/writes", "SQL")
    Rel(fetcher, shm, "writes", "shm_open and mmap")
    Rel(parser, shm, "reads", "shm_open and mmap")
    Rel(fetcher, openmeteo, "HTTP GET", "HTTPS")
    Rel(fetcher, elpriset, "HTTP GET", "HTTPS")
    Rel(browser, gridguard, "HTTP requests", "port 8080")
    Rel(mobile, gridguard, "HTTP requests", "port 8080")
    Rel(curl, gridguard, "HTTP requests", "port 8080")

    Rel(gridguard, log_server, "writes", "append")
    Rel(fetcher, log_fetcher, "writes", "append")
    Rel(parser, log_parser, "writes", "append")
    Rel(watchdog, log_watchdog, "writes", "append")
```

---

## 11. Error Handling & Recovery

```mermaid
flowchart TD
    START([System Running])

    START --> MONITOR{Monitoring}

    MONITOR -->|Watchdog| CHECK_HB{Heartbeat OK?}
    CHECK_HB -->|Yes, every 5s| MONITOR
    CHECK_HB -->|No, timeout 15s| KILL[Kill server process]
    KILL --> BACKOFF[Exponential backoff:<br/>1s → 2s → 4s → 8s → max 60s]
    BACKOFF --> RESTART[Restart server]
    RESTART -->|Success| MONITOR
    RESTART -->|Fail 5 times| FATAL[Log fatal error<br/>Exit watchdog]

    MONITOR -->|Server| CHECK_FETCH{Fetcher alive?}
    CHECK_FETCH -->|Yes| MONITOR
    CHECK_FETCH -->|No, SIGCHLD| LOG_FETCH[LOG_ERROR: Fetcher crashed]
    LOG_FETCH --> CONTINUE[Continue running<br/>Next request will fail]
    CONTINUE --> MONITOR

    MONITOR -->|HTTP Worker| CHECK_TIMEOUT{Request timeout?}
    CHECK_TIMEOUT -->|No, <30s| MONITOR
    CHECK_TIMEOUT -->|Yes, >30s| TIMEOUT_ERR[WorkCompletion_Wait returns -1]
    TIMEOUT_ERR --> SEND_500[HTTP 500 Internal Server Error]
    SEND_500 --> CLOSE[close clientSocket]
    CLOSE --> MONITOR

    MONITOR -->|Database| CHECK_DB{SQLite error?}
    CHECK_DB -->|No| MONITOR
    CHECK_DB -->|Yes| RETRY[Retry operation]
    RETRY -->|Success| MONITOR
    RETRY -->|Fail 3 times| DB_ERR[LOG_ERROR and HTTP 500]
    DB_ERR --> MONITOR

    MONITOR -->|Signal| CHECK_SIG{SIGTERM/SIGINT?}
    CHECK_SIG -->|No| MONITOR
    CHECK_SIG -->|Yes| SHUTDOWN[Graceful shutdown]

    SHUTDOWN --> STOP_ACCEPT[Stop accepting new connections]
    STOP_ACCEPT --> KILL_CHILDREN[Send SIGTERM to Fetcher & Parser]
    KILL_CHILDREN --> WAIT[waitpid for children]
    WAIT --> CLEANUP_IPC[unlink FIFO & socket<br/>shm_unlink shared memory]
    CLEANUP_IPC --> CLOSE_DB[sqlite3_close database]
    CLOSE_DB --> EXIT([Exit cleanly])

    style FATAL fill:#ff6b6b
    style EXIT fill:#90EE90
    style MONITOR fill:#87CEEB
```

---

## 12. Performance Characteristics

```mermaid
graph TB
    subgraph "Latency Breakdown - Typical Request"
        L1[HTTP Parse: 0.5 ms]
        L2[JWT Validation: 1.0 ms]
        L3[Database Read: 0.8 ms]
        L4[IPC Submit: 0.1 ms]

        L5[Fetcher: 150-300 ms<br/>API calls or cache hit 0.1ms]
        L6[Parser: 5-10 ms<br/>JSON parsing]
        L7[Compute: 0.16 ms<br/>Algorithm execution]

        L8[JSON Serialization: 1.5 ms]
        L9[HTTP Response: 0.5 ms]
    end

    L1 --> L2 --> L3 --> L4
    L4 -.pipeline.-> L5 --> L6 --> L7 --> L8
    L8 -.wake.-> L9

    TOTAL["Total latency:<br/>Best case with cache: ~10 ms<br/>Typical with API calls: 160-320 ms<br/>Worst case timeout: 30,000 ms"]

    L9 --> TOTAL

    subgraph "Throughput"
        T1[ThreadPool: 20 workers]
        T2[Max concurrent requests: 20]
        T3[Per-request time: ~200 ms avg]
        T4[Theoretical max: 100 req/s]
        T5[Realistic with cache: 200-300 req/s]
        T6[Bottleneck: Compute mutex<br/>if removed: over 2000 req/s]
    end

    T1 --> T2 --> T3 --> T4 --> T5
    T5 -.->|optimization| T6

    style TOTAL fill:#FFE4B5
    style T6 fill:#98FB98
```

---

## 13. External Dependencies

```mermaid
graph TB
    subgraph "GridGuard Dependencies"
        GG[GridGuard Server]
    end

    subgraph "System Libraries"
        LIBC[glibc<br/>POSIX functions]
        PTHREAD[libpthread<br/>Threading]
        RT[librt<br/>POSIX IPC]
    end

    subgraph "Third-Party Libraries"
        MBEDTLS[mbedtls<br/>TLS/HMAC-SHA256]
        SQLITE[libsqlite3<br/>Database]
        CJSON[cJSON<br/>JSON parsing<br/>Bundled]
    end

    subgraph "External APIs"
        OPENMETEO[Open-Meteo<br/>api.open-meteo.com<br/>Weather forecast]
        ELPRISET[Elpriset.se<br/>www.elpriset.se<br/>Electricity prices]
    end

    subgraph "Operating System"
        KERNEL[Linux Kernel<br/>fork, exec, pipe,<br/>FIFO, sockets,<br/>shared memory]
    end

    GG --> LIBC
    GG --> PTHREAD
    GG --> RT
    GG --> MBEDTLS
    GG --> SQLITE
    GG --> CJSON

    GG -.HTTPS.-> OPENMETEO
    GG -.HTTPS.-> ELPRISET

    LIBC --> KERNEL
    PTHREAD --> KERNEL
    RT --> KERNEL

    style OPENMETEO fill:#FFE4B5
    style ELPRISET fill:#FFE4B5
    style KERNEL fill:#87CEEB
    style CJSON fill:#98FB98
```

---

## 14. Example Request Timeline

```mermaid
gantt
    title HTTP /forecast Request Timeline (Total: ~210ms)
    dateFormat X
    axisFormat %L ms

    section HTTP Worker
    Parse HTTP request           :a1, 0, 1
    Validate JWT                 :a2, after a1, 2
    Read UserConfig from DB      :a3, after a2, 1
    Submit to pipeline           :a4, after a3, 1
    Wait on WorkCompletion       :crit, a5, after a4, 206
    Send HTTP response           :a6, after a5, 1

    section Fetcher
    Read from pipe               :b1, 4, 1
    Check cache (MISS)           :b2, after b1, 1
    API call Open-Meteo          :crit, b3, after b2, 120
    API call Elpriset            :crit, b4, after b3, 80
    Store in cache               :b5, after b4, 1
    Write to FIFO                :b6, after b5, 1

    section Parser
    Read from FIFO               :c1, 207, 1
    Parse weather JSON           :c2, after c1, 3
    Parse price JSON             :c3, after c2, 3
    Merge to ForecastData        :c4, after c3, 1
    Write to socket              :c5, after c4, 1

    section Compute
    Read from socket             :d1, 215, 1
    Find WorkCompletion          :d2, after d1, 1
    Run algorithm (NOCT)         :d3, after d2, 1
    Serialize to JSON            :d4, after d3, 2
    Signal WorkCompletion        :d5, after d4, 1
```

**Key takeaways:**
- Total: ~210 ms (typical with API calls)
- Fetcher dominates: ~200 ms (API calls)
- With cache hit: ~10 ms total!
- Compute algorithm: only 0.16 ms (negligible)

---

## 15. Security Model

```mermaid
flowchart TB
    START([HTTP Request])

    START --> PUBLIC{Public endpoint?}

    PUBLIC -->|Yes: /health| ALLOW1[Allow without auth]
    ALLOW1 --> HANDLE[Handle request]

    PUBLIC -->|No| CHECK_JWT{Has Authorization<br/>header?}

    CHECK_JWT -->|No| REJECT1[401 Unauthorized]
    CHECK_JWT -->|Yes| EXTRACT[Extract Bearer token]

    EXTRACT --> VALIDATE{JWT_Validate?}

    VALIDATE -->|Fail| REJECT2[401 Unauthorized<br/>Invalid signature or expired]

    VALIDATE -->|Success| CLAIMS[Extract claims.subject<br/>userId]

    CLAIMS --> AUTHZ{Authorization check}

    AUTHZ -->|/user/config| CHECK_OWN1{userId == claims.subject?}
    CHECK_OWN1 -->|No| REJECT3[403 Forbidden]
    CHECK_OWN1 -->|Yes| ALLOW2[Allow]

    AUTHZ -->|/forecast| CHECK_OWN2{userId == claims.subject?}
    CHECK_OWN2 -->|No| REJECT4[403 Forbidden]
    CHECK_OWN2 -->|Yes| ALLOW3[Allow]

    AUTHZ -->|/schedule| CHECK_OWN3{userId == claims.subject?}
    CHECK_OWN3 -->|No| REJECT5[403 Forbidden]
    CHECK_OWN3 -->|Yes| ALLOW4[Allow]

    ALLOW2 --> HANDLE
    ALLOW3 --> HANDLE
    ALLOW4 --> HANDLE

    HANDLE --> INPUT{Input validation}

    INPUT -->|Invalid lat/lon| REJECT6[400 Bad Request]
    INPUT -->|Invalid solar params| REJECT7[400 Bad Request]
    INPUT -->|Invalid JSON| REJECT8[400 Bad Request]

    INPUT -->|Valid| PROCESS[Process request]

    PROCESS --> RESPONSE[200 OK with JSON]

    style REJECT1 fill:#ff6b6b
    style REJECT2 fill:#ff6b6b
    style REJECT3 fill:#ff6b6b
    style REJECT4 fill:#ff6b6b
    style REJECT5 fill:#ff6b6b
    style REJECT6 fill:#ffa500
    style REJECT7 fill:#ffa500
    style REJECT8 fill:#ffa500
    style RESPONSE fill:#90EE90
```

---

## Sammanfattning

Detta dokument visualiserar hela GridGuard-systemets arkitektur med 15 olika Mermaid-diagram som täcker:

1. **Systemöversikt** - Multi-process arkitektur med 4 processer
2. **Startup** - Komplett sekvens från Watchdog till Server ready
3. **Request Flow** - 30+ steg från HTTP request till response
4. **IPC** - Alla kommunikationsmekanismer (pipe, FIFO, socket, shared memory)
5. **Thread Sync** - WorkCompletion state machine
6. **Compute Algorithm** - Detaljerat flödesschema med alla 3 passes
7. **Database** - ER-diagram med user_configs och schedules
8. **API** - Alla endpoints med exempel på responses
9. **Data Structures** - Class diagram för alla core models
10. **Deployment** - C4-diagram för production deployment
11. **Error Handling** - Felhantering och recovery-strategier
12. **Performance** - Latency breakdown och throughput
13. **Dependencies** - Alla external dependencies
14. **Timeline** - Gantt chart för en typisk request
15. **Security** - Autentisering och auktorisering

**Total täckning:** 100% av systemet är nu visualiserat!

---

**Genererat:** 2026-03-03
**Format:** Mermaid (compatible med GitHub, GitLab, VS Code, Obsidian)
