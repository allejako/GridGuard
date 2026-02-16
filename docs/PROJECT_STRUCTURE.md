# GridGuard Projektstruktur

*Uppdaterad: 2026-02-16*

## Mappstruktur

```
GridGuard/
├── src/
│   ├── server/                 # Server entry + orchestration
│   │   ├── main.c              # Entry point (34 rader)
│   │   ├── Server.h/c          # Äger alla komponenter
│   │   └── ClientHandler.h/c   # Client state machine
│   │
│   ├── pipeline/               # Data processing pipeline
│   │   ├── PipelineOrchestrator.h/c  # Koordinerar stages
│   │   ├── stages/
│   │   │   ├── FetchStage.h/c        # Hämtar data från API
│   │   │   ├── ParseStage.h/c        # Parsar JSON
│   │   │   └── ComputeStage.h/c      # Beräknar energiplan
│   │   └── components/
│   │       ├── Fetcher.h/c           # HTTP/curl wrapper
│   │       ├── Parser.h/c            # cJSON wrapper
│   │       └── Compute.h/c           # Energiberäkningar (mock)
│   │
│   ├── concurrency/            # Trådhantering
│   │   ├── sync/
│   │   │   └── Queue.h/c             # Trådsäker producer-consumer kö
│   │   └── pool/
│   │       └── ThreadPool.h/c        # Worker threads för klienter
│   │
│   ├── tcp/                    # Nätverkskommunikation
│   │   ├── TCPServer.h/c             # Server socket
│   │   └── TCPClient.hpp/cpp         # C++ klient
│   │
│   ├── api/                    # Externa API:er
│   │   └── APIEndpoints.h/c          # URL-byggare
│   │
│   ├── data/                   # Datastrukturer
│   │   ├── EnergyData.h/c            # Energiplan struct
│   │   ├── OpenMeteoData.h           # Väderdata struct
│   │   └── ElprisetData.h            # Spotprisdata struct
│   │
│   ├── common/                 # Gemensamma verktyg
│   │   ├── Logger.h/c                # Färgkodad loggning
│   │   └── SignalHandler.h/c         # SIGINT/SIGTERM
│   │
│   ├── client/                 # C++ klient
│   │   └── main.cpp                  # Klient entry point
│   │
│   ├── libs/                   # Tredjepartsbibliotek
│   │   └── cJSON.h/c                 # JSON-parser
│   │
│   └── tests/                  # Tester
│       ├── test_api_fetch.c          # API + parsing test
│       ├── test_logger.c             # Logger test
│       └── test_pipeline.c           # Pipeline test
│
├── config/
│   └── Config.h                # Konfigurationskonstanter
│
├── docs/                       # Dokumentation
├── build/                      # Kompilerade objekt (gitignored)
├── bin/                        # Executables (gitignored)
├── logs/                       # Loggfiler (gitignored)
│
├── Makefile                    # Build-system
└── README.md
```

## Arkitektur

```
main.c → Server
           ├── TCPServer      (accepterar klienter)
           ├── ThreadPool     (hanterar klienter med select())
           │     └── ClientHandler (state machine)
           └── Pipeline       (3 worker-trådar)
                 ├── FetchStage  → requestQueue → fetchQueue
                 ├── ParseStage  → fetchQueue   → parseQueue
                 └── ComputeStage→ parseQueue   → client socket
```

## Viktiga komponenter

| Komponent | Fil | Ansvar |
|-----------|-----|--------|
| Server | `server/Server.c` | Äger och koordinerar alla komponenter |
| Pipeline | `pipeline/PipelineOrchestrator.c` | Startar 3 worker-trådar |
| Queue | `concurrency/sync/Queue.c` | Trådsäker FIFO med mutex/cond |
| ThreadPool | `concurrency/pool/ThreadPool.c` | Hanterar 20 workers x 50 klienter |
| ClientHandler | `server/ClientHandler.c` | Parsar kommandon, submittar requests |

## Dataflöde

1. **Client** → TCP connect → **ThreadPool** tilldelar worker
2. **Worker** → recv() kommando → **ClientHandler** parsar
3. **ClientHandler** → `Pipeline_SubmitRequest()` → **requestQueue**
4. **FetchStage** → hämtar väder + priser → **fetchQueue**
5. **ParseStage** → parsar JSON → **parseQueue**
6. **ComputeStage** → beräknar plan → `send()` till client

## Build

```bash
make              # Bygg server + klient
make test         # Kör alla tester
make clean        # Rensa build
make valgrind-server  # Minnesanalys
```

## Tester

```bash
make test-api      # API fetch + parsing
make test-logger   # Logger-funktionalitet
make test-pipeline # Multi-threaded pipeline
```

## Konfiguration (config/Config.h)

```c
SERVER_PORT          "8080"
MAX_THREADS          20
MAX_CLIENTS_PER_THREAD 50
WEATHER_API_BASE_URL "https://api.open-meteo.com/v1/forecast"
SPOTPRICE_API_BASE_URL "https://www.elprisetjustnu.se/api/v1/prices"
```
