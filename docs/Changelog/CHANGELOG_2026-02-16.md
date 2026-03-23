# Ändringslogg - 2026-02-16

## Övergripande förändringar

Idag har vi genomfört den stora mappstruktur-refaktoreringen (Alternativ A från förra changeloggen) och identifierat ett arkitekturproblem med cachningen som behöver åtgärdas.

## Implementerad mappstruktur (Alternativ A)

Hela projektet har omorganiserats enligt planen från 2026-02-12:

```
src/
  pipeline/
    PipelineOrchestrator.h/c     # Koordinerar pipeline stages
    stages/
      FetchStage.h/c             # Hämtar data från API
      ParseStage.h/c             # Parsar JSON till structs
      ComputeStage.h/c           # Beräknar energiplan
      CacheStage.h/c             # Sparar i cache + skickar svar
    components/
      Fetcher.h/c                # HTTP/curl wrapper
      Parser.h/c                 # cJSON wrapper
      Compute.h/c                # Energiberäkningar
      Cache.h/c                  # Cache-komponent

  concurrency/
    sync/
      Queue.h/c                  # Återanvändbar trådsäker kö
    pool/
      ThreadPool.h/c             # Worker threads för klienter

  server/
    main.c                       # Entry point (34 rader)
    Server.h/c                   # Orchestration
    ClientHandler.h/c            # Client state machine
```

**Fördelar med nya strukturen:**
- Queue är nu en återanvändbar modul (var tidigare inline i PipelineThreads.c)
- Varje stage har sin egen fil istället för 564 rader i en fil
- ClientHandler är separerad från ThreadPool
- Tydlig separation mellan stages/ (worker-logik) och components/ (domänlogik)

## Identifierat arkitekturproblem: Cache på fel nivå

Vid genomgång av cachningen upptäcktes ett designproblem:

**Nuvarande implementation:**
- Cache sparar färdiga `EnergyData` (energiplaner) efter ComputeStage
- Problemet: Energiplaner är klient-specifika (baserade på antal solpaneler, batteristorlek, etc.)
- En cachad plan för Klient A är värdelös för Klient B

**Korrekt approach:**
- Cache bör ligga i FetchStage
- Cacha väderdata per plats (t.ex. "stockholm")
- Cacha spotpriser per region (t.ex. "SE3")
- Dessa är gemensamma för alla klienter i samma område

Se `docs/CACHEPLAN.md` för fullständig implementeringsplan.

## Övriga ändringar

- **Logger-fix:** Ändrade loggpath från `"../../logs"` till `"logs/server.log"` - relativa sökvägar fungerade inte korrekt
- **Dokumentation:** Uppdaterade PROJECT_STRUCTURE.md och SAMMANFATTNING.md med ny mappstruktur
- **Pipeline:** Nu 4 trådar (Fetch, Parse, Compute, Cache) - men Cache-tråden kommer tas bort vid cache-refaktoreringen

## IPC (Inter-Process Communication) i projektet

Kursen täcker IPC i vecka 4-5. Här är hur vi använder och kan utöka IPC i GridGuard:

### Nuvarande IPC-användning

| Teknik | Var | Beskrivning |
|--------|-----|-------------|
| **TCP Sockets** | `src/tcp/TCPServer.c` | Server lyssnar på port 8080, klienter ansluter via TCP |
| **TCP Sockets** | `src/tcp/TCPClient.cpp` | C++ klient ansluter till servern |

```
┌─────────────┐     TCP/IP      ┌─────────────┐
│   Klient    │ ◄─────────────► │   Server    │
│  (process)  │   socket()      │  (process)  │
│             │   connect()     │             │
│             │   send/recv     │             │
└─────────────┘                 └─────────────┘
```

**Relevanta funktioner i koden:**
- `socket()`, `bind()`, `listen()`, `accept()` - server-sidan (`TCPServer.c`)
- `socket()`, `connect()`, `send()`, `recv()` - klient-sidan (`TCPClient.cpp`)
- `getaddrinfo()` - för IPv4/IPv6-stöd

### Intern trådkommunikation (ej IPC, men relaterat)

Inom servern använder vi **trådar** (inte processer), så tekniskt sett är detta inte IPC utan trådsynkronisering:

| Teknik | Var | Beskrivning |
|--------|-----|-------------|
| **Mutex + Condition Variables** | `src/concurrency/sync/Queue.c` | Producer-consumer köer mellan pipeline-trådar |
| **pthread** | Hela servern | 4 pipeline-trådar + 20 worker-trådar |

```
FetchThread ──► fetchQueue ──► ParseThread ──► parseQueue ──► ComputeThread
                 (mutex)                        (mutex)
```

### Möjliga IPC-utökningar för examination

Om ni vill demonstrera fler IPC-tekniker för kursen:

#### Alternativ 1: Shared Memory för cache (rekommenderas)

Använd POSIX shared memory (`shm_open`, `mmap`) för att dela cache mellan processer:

```c
// Skapa delat minne för cache
int fd = shm_open("/gridguard_cache", O_CREAT | O_RDWR, 0666);
ftruncate(fd, sizeof(SharedCache));
SharedCache *cache = mmap(NULL, sizeof(SharedCache),
                          PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

// Använd semaforer för synkronisering
sem_t *sem = sem_open("/gridguard_sem", O_CREAT, 0666, 1);
```

**Fördelar:**
- Demonstrerar shared memory (kursmål)
- Cache överlever server-omstarter
- Flera server-instanser kan dela cache

#### Alternativ 2: Named Pipes för loggning

Använd FIFO (named pipe) för att skicka loggar till en separat logg-process:

```c
// Skapa named pipe
mkfifo("/tmp/gridguard_log", 0666);

// Server skriver till pipe
int log_fd = open("/tmp/gridguard_log", O_WRONLY);
write(log_fd, log_message, strlen(log_message));

// Separat logg-process läser
int log_fd = open("/tmp/gridguard_log", O_RDONLY);
read(log_fd, buffer, sizeof(buffer));
```

#### Alternativ 3: Unix Domain Sockets (lokalt)

Snabbare än TCP för lokal kommunikation:

```c
struct sockaddr_un addr;
addr.sun_family = AF_UNIX;
strcpy(addr.sun_path, "/tmp/gridguard.sock");
```

### Rekommendation

För examination, fokusera på:

1. **TCP Sockets** - Redan implementerat, dokumentera väl
2. **Shared Memory** - Lägg till för cachen (extra poäng för IPC-demonstration)
3. **Mutex/Condition Variables** - Redan implementerat i Queue.c

**Viktigt:** Se till att kunna förklara skillnaden mellan:
- IPC (kommunikation mellan **processer**)
- Trådsynkronisering (kommunikation mellan **trådar** i samma process)

## Prioriteringar

### Hög prioritet
1. **Cache-refaktorering** - Implementera CACHEPLAN.md (flytta cache till FetchStage)
2. **C++ klient** - Nuvarande klient är minimal (~11 rader), behöver RAII och STL
3. **Compute-modul** - Fortfarande mock, behöver riktiga energiberäkningar

### Medium prioritet
4. **Tester** - Fler enhetstester, särskilt för nya cache-komponenter
5. **Thread-safety** - Lägg till mutex i Fetcher, Parser, Compute (enligt förra changeloggen)

### Låg prioritet
6. **Dokumentation** - Doxygen-kommentarer
7. **Profilering** - Kommer i vecka 10

## Individuellt vs grupparbete

### Lämpligt för individuellt arbete

Dessa uppgifter är relativt isolerade och kan göras utan att krocka med andras kod:

| Uppgift | Filer | Kommentar |
|---------|-------|-----------|
| C++ klient med RAII | `src/tcp/TCPClient.hpp/cpp` | Helt separat från servern |
| WeatherCache implementation | `src/pipeline/components/WeatherCache.h/c` | Ny fil, ingen konflikt |
| PriceCache implementation | `src/pipeline/components/PriceCache.h/c` | Ny fil, ingen konflikt |
| Enhetstester | `src/tests/test_*.c` | Isolerade testfiler |
| Doxygen-kommentarer | Valfria filer | Ändrar inte funktionalitet |

**Tips:** Om du jobbar individuellt, skapa en feature branch:
```bash
git checkout -b feature/cpp-client
# ... arbeta ...
git push origin feature/cpp-client
```

### Bättre att göra tillsammans som grupp

Dessa uppgifter påverkar flera filer och kräver koordinering:

| Uppgift | Varför tillsammans? |
|---------|---------------------|
| Cache-refaktorering (CACHEPLAN.md) | Ändrar FetchStage, ComputeStage, PipelineOrchestrator, tar bort CacheStage |
| Compute-modulen | Påverkar hur data flödar genom hela pipelinen |
| Arkitekturförändringar | Risk för merge-konflikter om flera jobbar parallellt |
| Makefile-ändringar | En person bör "äga" buildsystemet |

**Rekommendation:** Sitt tillsammans (eller screenshare) när ni gör cache-refaktoreringen. Det är ~10 filer som behöver ändras koordinerat.

## Nästa steg

1. Bestäm vem som gör vad (individuellt vs grupp)
2. Implementera cache-refaktoreringen (helst tillsammans)
3. Påbörja C++ klient med RAII (kan göras parallellt)
4. Skriv tester för nya cache-komponenter

## Bygga och testa

```bash
make clean && make      # Bygger allt
make test               # Kör tester
make run-server         # Startar servern
```

Alla tester passerar efter dagens ändringar.

---

## Cache-steg i pipeline

### Nytt pipeline-steg: Cache
Pipeline:n har utökats från tre steg till fyra: `fetch → parse → compute → cache`, enligt projektspecifikationen.

### Nya filer
- `src/pipeline/components/Cache.h/c` — Cache-komponent med TTL-stöd (Time-To-Live)
- `src/pipeline/stages/CacheStage.h/c` — Cache-trådens worker-funktion

### Ändringar
- `PipelineOrchestrator.h/c` — Lagt till `cacheThread`, `computeQueue` och `Cache` i Pipeline-structen. Initierar och startar cache-tråden.
- `ComputeStage.c` — Kollar cache innan beräkning (cache hit = skippar Compute). Pushar resultat till `computeQueue` istället för att skicka direkt till klient.

### Hur cachen fungerar
- Energiplaner lagras med nyckeln `location/region` (t.ex. `"SE3/SE3"`)
- TTL på 15 minuter — efter det räknas datan som utgången
- Cache-lookup sker i Compute-steget innan beräkning
- Cache-lagring sker i Cache-steget efter att resultatet tagits emot
- Mutex-skyddad för trådsäkerhet
- Max 64 entries, äldsta ersätts om cachen är full

### Testat
- Första anropet: Cache MISS → hämtar från API → sparar i cache
- Andra anropet: Cache HIT → skippar API och beräkning → returnerar cachad data

---
# Ändringslogg - 2026-02-16

## Cache-steg i pipeline

### Nytt pipeline-steg: Cache
Pipeline:n har utökats från tre steg till fyra: `fetch → parse → compute → cache`, enligt projektspecifikationen.

### Nya filer
- `src/pipeline/components/Cache.h/c` — Cache-komponent med TTL-stöd (Time-To-Live)
- `src/pipeline/stages/CacheStage.h/c` — Cache-trådens worker-funktion

### Ändringar
- `PipelineOrchestrator.h/c` — Lagt till `cacheThread`, `computeQueue` och `Cache` i Pipeline-structen. Initierar och startar cache-tråden.
- `ComputeStage.c` — Kollar cache innan beräkning (cache hit = skippar Compute). Pushar resultat till `computeQueue` istället för att skicka direkt till klient.

### Hur cachen fungerar
- Energiplaner lagras med nyckeln `location/region` (t.ex. `"SE3/SE3"`)
- TTL på 15 minuter — efter det räknas datan som utgången
- Cache-lookup sker i Compute-steget innan beräkning
- Cache-lagring sker i Cache-steget efter att resultatet tagits emot
- Mutex-skyddad för trådsäkerhet
- Max 64 entries, äldsta ersätts om cachen är full

### Testat
- Första anropet: Cache MISS → hämtar från API → sparar i cache
- Andra anropet: Cache HIT → skippar API och beräkning → returnerar cachad data
