# Ändringslogg - 2026-02-17

## Övergripande förändringar

Idag har vi genomfört en större omstrukturering av projektet med fokus på professionell mapporganisation och lagt grunden för daemon/watchdog-implementation. Dessutom har vi dokumenterat arkitekturen för framtida processhantering och börjat planera datahanteringsstrategin.

---

## Stor omstrukturering av src/-mappen

Vi har gått från en ad-hoc mappstruktur till en väl genomtänkt lagerbaserad arkitektur som tydligt separerar olika ansvarsområden.

### Tidigare struktur (problem)

```
src/
  common/              # För brett - Logger och SignalHandler har olika syften
  api/                 # Oklart om det är nätverks- eller business logic
  server/tcp/          # Förvirrande - TCPClient låg i server-mappen
  config/              # Låg utanför src/, svårt att hitta
  tests/               # Inga kategorier - allt i en mapp
```

**Huvudproblemen:**
- Otydliga ansvarsområden - vad hör hemma var?
- Svårt att hitta kod - är det infrastruktur eller business logic?
- Inte skalbart - var lägger man daemon/watchdog?
- Ingen tydlig separation mellan "vad systemet gör" och "hur det körs"

### Ny struktur (implementerad)

```
src/
├── application/          # "Vad gör GridGuard?"
│   ├── api/             # Flyttad från src/api
│   ├── configs/         # Flyttad från config/, namnändring för user configs
│   ├── core/
│   ├── models/
│   ├── services/
│   └── workers/
│
├── infrastructure/       # "Hur körs GridGuard?" (NYTT!)
│   ├── daemon/          # Placeholder för daemon implementation
│   ├── logging/         # Flyttad från common/Logger
│   ├── signals/         # Flyttad från common/SignalHandler
│   └── watchdog/        # Placeholder för watchdog implementation
│
├── network/             # "Hur kommunicerar GridGuard?" (NYTT!)
│   ├── client/          # TCPClient flyttad från server/tcp/
│   └── server/          # TCPServer flyttad från server/tcp/
│
├── concurrency/         # "Hur parallelliseras GridGuard?"
│   ├── ipc/            # Placeholder för pipes, shared memory (vecka 4-5)
│   ├── sync/
│   └── threads/
│
├── server/             # Entry point
├── client/             # C++ klient
├── libs/               # Third-party
└── tests/              # Tests
    ├── unit/           # test_logger.c
    └── integration/    # test_api_fetch.c, test_pipeline.c
```

### Namnändring: config → configs

**Anledning:**
- Idag innehåller mappen bara `Config.h` med hårdkodade konstanter
- Imorgon ska vi lägga till user-specifika konfigurationer här
- Plural-namnet `configs/` indikerar att det kan finnas flera konfigurationsfiler

**Framtida innehåll:**
```
src/application/configs/
├── Config.h              # System-wide defaults (SERVER_PORT, API_URLS, etc.)
├── UserConfig.h          # User-specific settings (template)
└── ConfigLoader.c/h      # Ladda user configs från fil/databas (framtida)
```

**Varför viktigt:**
- Just nu är allt hårdkodat: antal solpaneler, batteristorlek, konsumption
- Det gör att alla användare får samma energiberäkning
- Vi måste göra systemet användarspecifikt så varje klient kan ha sina egna inställningar

### Fördelar med nya strukturen

✅ **Tydliga ansvarsområden** - Varje top-level mapp har ett klart syfte
✅ **Skalbar** - Lätt att lägga till daemon, watchdog, IPC utan att bli rörigt
✅ **Navigerbar** - Man vet direkt var man ska leta efter viss kod
✅ **Professionell** - Följer industry best practices
✅ **Kursanpassad** - Tydliga platser för processer, IPC, daemon, watchdog

### Tekniska detaljer

- **Alla filer flyttade med `git mv`** - Historiken bevaras
- **Makefile uppdaterad** - Nya include-paths, build directories, source patterns
- **Bygg-verifierat** - Både server och klient bygger utan fel
- **Tests uppdaterade** - Organiserade i unit/ och integration/
- **Dokumentation** - PROJECT_STRUCTURE.md fullständigt uppdaterad

---

## Daemon & Watchdog - Dokumentation och arkitektur

Vi har skapat en omfattande guide för hur vi ska implementera daemon och watchdog för att göra GridGuard till en produktionsklar systemtjänst.

### Dokumentation skapad

**`docs/DAEMON_WATCHDOG.md`** - 380+ rader pedagogisk guide som täcker:

1. **Varför processer?** - Förklarar gapet i vår nuvarande arkitektur
2. **Systemarkitektur** - 4 Mermaid-diagram som visar:
   - Komplett systemöversikt med watchdog + daemon
   - Process-hierarki (systemd → watchdog → daemon → threads)
   - IPC-flöden (pipes, signals, PID-filer)
   - Krasch-hantering och restart-logik

3. **Daemon-konceptet** - Vad det är och hur det fungerar:
   - 7-stegs daemonize-process (fork, setsid, fork, chdir, close fds, etc.)
   - PID-filhantering
   - Signal handlers (SIGTERM, SIGHUP, SIGPIPE)

4. **Watchdog-konceptet** - Process monitoring och recovery:
   - fork() + exec() för att starta daemon
   - waitpid() för att detektera krascher
   - Automatisk omstart med begränsningar (max 5 på 5 min)
   - Heartbeat-mekanik via pipes eller signals

5. **IPC mellan processer** - Tre metoder:
   - **Signals** - SIGTERM, SIGHUP, SIGUSR1 för heartbeat
   - **Pipes** - Heartbeat-data via pipe mellan watchdog och daemon
   - **Unix Domain Sockets** - För mer avancerad kommunikation

6. **Praktiska implementationsdetaljer:**
   - fork/exec/wait - vad händer egentligen?
   - Restart-logik och crash-loop protection
   - Systemd integration (service files)
   - Test-scenarion

7. **Kursmål coverage** - Mappar till:
   - Vecka 1: Processer (fork, exec, wait, PID)
   - Vecka 4: Pipes (heartbeat communication)
   - Vecka 5: Signals och graceful shutdown
   - Kursmål 8: IPC för processkommunikation

### Varför daemon/watchdog är viktigt

**Nuvarande begränsningar:**
- Servern måste köras i en öppen terminal
- Om servern kraschar måste någon manuellt starta om den
- Ingen automatisk start vid boot
- Kan inte köra som systemtjänst (t.ex. på en Raspberry Pi i ett smarthus)

**Efter implementation:**
- ✅ Servern kan köra i bakgrunden (daemon)
- ✅ Automatisk återstart vid krasch (watchdog)
- ✅ Kan startas automatiskt vid boot (systemd)
- ✅ Produktionsklar arkitektur
- ✅ Täcker processer och IPC från kursen (som vi annars missar)

### Kursmål coverage

Vi använder just nu **bara trådar** (pthread). Men kursen täcker:
- **Vecka 1:** Processer (fork, exec, wait) - Vi missar detta
- **Vecka 4:** Pipes - Vi missar detta
- **Vecka 5:** Delat minne, signals - Vi missar delvis

Daemon + watchdog ger oss:
```
✓ fork() - Watchdog startar daemon
✓ exec() - Execar daemon-programmet
✓ wait()/waitpid() - Watchdog väntar på daemon
✓ Pipes - Heartbeat-kommunikation
✓ Signals - SIGTERM, SIGHUP, SIGUSR1
✓ PID-filer - Process management
✓ Session leaders - setsid()
```

---

## Datahantering - Strategi och beslut

Vi behöver fatta beslut om hur vi ska lagra och hantera data. Det påverkar cache, user configs och framtida features.

### Nuvarande situation

**Cache (in-memory):**
```c
typedef struct {
    char location[64];
    char region[16];
    EnergyData plan;
    time_t timestamp;
    bool valid;
} CacheEntry;

CacheEntry cache[MAX_CACHE_ENTRIES];  // Array i minnet
```

**Problem:**
- Allt försvinner vid server-restart
- Ingen persistens
- Begränsad storlek (array)
- Ingen sökfunktionalitet

### Alternativ för datalagring

#### Alternativ 1: JSON (Enklast)

**Fördelar:**
- Vi använder redan cJSON för parsing
- Mänskligt läsbar
- Enkelt att debugga
- Perfekt för små configs

**Nackdelar:**
- Långsamt för stora datasets
- Måste läsa/skriva hela filen
- Ingen indexering eller sökning
- Inte lämpligt för cache med tusentals entries

**Användningsområden:**
```
✓ User configs (solpaneler, batteri, konsumption)
✓ System settings
✗ Cache (för långsamt)
✗ Historisk data
```

**Exempel:**
```json
// configs/users/user_123.json
{
  "userId": "user_123",
  "location": "Stockholm",
  "region": "SE3",
  "solar": {
    "panelAreaM2": 20.0,
    "efficiency": 0.18
  },
  "battery": {
    "capacityKwh": 10.0,
    "maxChargeRateKw": 5.0
  },
  "consumption": {
    "baseLoadKw": 1.2
  }
}
```

#### Alternativ 2: SQLite (Rekommenderas för cache)

**Fördelar:**
- Ingen server behövs (embedded database)
- SQL-queries för sökning och filtrering
- Transaktioner (ACID)
- Indexering för snabba lookups
- Begränsad storlek (~140 TB teoretiskt)

**Nackdelar:**
- Kräver SQL-kunskap
- Lite mer overhead än in-memory
- Måste länka libsqlite3

**Användningsområden:**
```
✓ Cache (snabba lookups: "SELECT * WHERE location = 'Stockholm'")
✓ User database
✓ Historisk data
✗ Real-time sensor data (för långsamt)
```

**Exempel:**
```sql
CREATE TABLE cache (
    id INTEGER PRIMARY KEY,
    location TEXT NOT NULL,
    region TEXT NOT NULL,
    weather_data TEXT,  -- JSON blob
    spot_prices TEXT,   -- JSON blob
    timestamp INTEGER,
    UNIQUE(location, region)
);

-- Snabb lookup
SELECT weather_data FROM cache
WHERE location = 'Stockholm'
  AND timestamp > (strftime('%s', 'now') - 3600);
```

#### Alternativ 3: HDF5 (För forskning/vetenskaplig data)

**Fördelar:**
- Extremt effektivt för stora numeriska dataset
- Hierarkisk struktur
- Komprimering
- Används inom meteorologi, vetenskap

**Nackdelar:**
- Overkill för vårt use-case
- Kräver HDF5-bibliotek
- Komplex API
- Inte lämpligt för text/configs

**När det är relevant:**
```
✓ Lagra års-värden av väderdata (miljoner datapunkter)
✓ Tidsserieanalys
✗ User configs
✗ Cache (för komplext)
```

### Rekommenderad strategi

| Data-typ | Lagring | Anledning |
|----------|---------|-----------|
| **User configs** | JSON-filer | Enkelt, mänskligt läsbart, små filer |
| **System config** | Config.h | Kompileringskonstanter |
| **Cache (weather/prices)** | SQLite | Snabb lookup, persistens, SQL-queries |
| **Historisk data** | SQLite eller CSV | Enkel export, långtidslagring |
| **Real-time state** | In-memory structs | Snabbast, ingen I/O overhead |

### Implementation plan

**Fas 1: User configs (JSON)**
```
1. Skapa UserConfig.h struct
2. Implementera ConfigLoader.c med cJSON
3. Läs user config vid client connect
4. Skicka config till Compute-worker
```

**Fas 2: Persistent cache (SQLite)**
```
1. Länka libsqlite3 i Makefile
2. Skapa CacheDB.c wrapper runt SQLite
3. Konvertera nuvarande in-memory cache till SQLite
4. Implementera TTL (time-to-live) för cache entries
```

**Fas 3: Historisk data (optional)**
```
1. Logga alla requests till SQLite
2. Skapa export-funktion till CSV
3. Analysera användarmönster
```

---

## Cache - Nuvarande status

Från förra changeloggen (2026-02-16) identifierades ett arkitekturproblem med cache.

### Problemet (fortfarande aktuellt)

**Nuvarande implementation:**
- Cache sparar färdiga `EnergyData` (energiplaner) efter ComputeStage
- Problemet: Energiplaner är user-specifika (olika solpaneler, batterier)
- En cachad plan för User A är värdelös för User B
- Vi cachar fel data

### Lösningen (från CACHEPLAN.md)

**Rätt approach:**
- Cache ska ligga i FetchStage
- Cacha **väderdata** per plats (t.ex. "Stockholm")
- Cacha **spotpriser** per region (t.ex. "SE3")
- Detta är gemensamt för alla users i samma område

**Exempel:**
```
User A i Stockholm: Hämtar väder → Cache HIT → Använder cached data → Compute med sina settings
User B i Stockholm: Hämtar väder → Cache HIT → Använder SAMMA cached data → Compute med SINA settings
User C i Göteborg:  Hämtar väder → Cache MISS → Fetchar nytt → Cache uppdateras
```

### Integration med databas-strategi

När vi implementerar SQLite-cache:

```sql
-- Weather cache
CREATE TABLE weather_cache (
    location TEXT PRIMARY KEY,
    latitude REAL,
    longitude REAL,
    forecast_json TEXT,  -- Hela JSON-responsen från Open-Meteo
    fetched_at INTEGER,
    expires_at INTEGER
);

-- Spot price cache
CREATE TABLE price_cache (
    region TEXT NOT NULL,
    date TEXT NOT NULL,
    prices_json TEXT,
    fetched_at INTEGER,
    PRIMARY KEY (region, date)
);
```

**Fördelar:**
- Snabba lookups: `SELECT forecast_json FROM weather_cache WHERE location = ? AND expires_at > ?`
- Automatisk expiry med indexes
- Flera server-instanser kan dela samma cache-databas
- Överlever server-restarts

### Prioritet

**Hög prioritet** - Detta måste fixas innan systemet är användbart med riktiga users.

---

## Förbättringar för nästa commit

### 1. User-specific configs (Hög prioritet)

**Uppgift:** Ta bort hårdkodad data och gör systemet user-specific

**Vad ska göras:**
```
1. Definiera UserConfig struct i configs/UserConfig.h
   - userId
   - location/coordinates
   - solarConfig (panelAreaM2, efficiency)
   - batteryConfig (capacity, chargeRate)
   - consumptionProfile (baseLoad, peakLoad)

2. Implementera ConfigLoader.c
   - Läs JSON från configs/users/{userId}.json
   - Validera data
   - Returnera UserConfig struct

3. Uppdatera ClientHandler
   - Parse userId från klient-request
   - Ladda user config
   - Skicka config till workers

4. Uppdatera Compute
   - Ta emot UserConfig i WorkRequest
   - Använd user-specifika värden istället för hårdkodade
```

**Filer som påverkas:**
- `src/application/configs/UserConfig.h` (ny)
- `src/application/configs/ConfigLoader.c/h` (ny)
- `src/server/ClientHandler.c` (läs userId, ladda config)
- `src/application/workers/ComputeWorker.c` (använd user config)
- `src/application/services/Compute.c` (ta emot config som parameter)

**Vem kan göra:** 1-2 personer, relativt isolerat

---

### 2. Implementera daemon & watchdog (Hög prioritet för kursen)

**Uppgift:** Implementera daemon och watchdog enligt DAEMON_WATCHDOG.md

**Steg 1: Daemon basics**
```
1. Skapa infrastructure/daemon/Daemon.c/h
   - Daemon_Init() - daemonize-processen
   - Daemon_WritePidFile()
   - Daemon_RemovePidFile()
   - Signal handlers

2. Uppdatera src/server/main.c
   - Lägg till -d flagga (command line parsing)
   - Anropa Daemon_Init() om -d satt
```

**Steg 2: Watchdog process**
```
1. Skapa infrastructure/watchdog/Watchdog.c/h + main.c
   - Watchdog_Init()
   - Watchdog_StartDaemon() - fork + exec
   - Watchdog_Monitor() - main loop med waitpid
   - Watchdog_RestartDaemon() - restart-logik

2. Uppdatera Makefile
   - Nytt target: bin/GridGuard-watchdog
```

**Steg 3: IPC - Heartbeat (optional, extra poäng)**
```
1. Implementera heartbeat via pipe eller signal
2. Lägg till timeout-detektering i watchdog
3. Testa krasch-scenarion
```

**Filer som skapas:**
- `src/infrastructure/daemon/Daemon.c/h`
- `src/infrastructure/watchdog/Watchdog.c/h`
- `src/infrastructure/watchdog/main.c`

**Vem kan göra:** 2 personer tillsammans (kräver koordination)

**Testplan:**
```bash
# Test 1: Daemon mode
./bin/GridGuard-server -d
ps aux | grep GridGuard  # Ska köra i bakgrunden
cat /var/run/gridguard.pid

# Test 2: Watchdog
./bin/GridGuard-watchdog
kill -9 $(cat /var/run/gridguard.pid)  # Simulera krasch
# Watchdog ska automatiskt starta om

# Test 3: Graceful shutdown
kill -TERM $(pgrep GridGuard-watchdog)
# Både watchdog och daemon ska stänga ner snyggt
```

---

### 3. Cache-refaktorering med SQLite (Hög prioritet)

**Uppgift:** Implementera persistent cache med SQLite enligt CACHEPLAN.md

**Steg 1: SQLite wrapper**
```
1. Skapa application/services/CacheDB.c/h
   - CacheDB_Init() - Öppna/skapa databas
   - CacheDB_GetWeather(location, &weatherData)
   - CacheDB_SetWeather(location, weatherData, ttl)
   - CacheDB_GetPrices(region, date, &priceData)
   - CacheDB_SetPrices(region, date, priceData)
   - CacheDB_Cleanup() - Ta bort expired entries

2. Uppdatera Makefile - länka libsqlite3
   LDFLAGS = -pthread -lcurl -lsqlite3
```

**Steg 2: Integrera med FetchWorker**
```
1. FetchWorker kollar CacheDB först
2. Om cache HIT → använd cached data
3. Om cache MISS → fetcha från API → spara i CacheDB
4. Ta bort nuvarande Cache.c/h (blir ersatt av CacheDB)
```

**Steg 3: Ta bort CacheWorker**
```
1. ComputeWorker skickar direkt till klient (ingen cache-tråd behövs)
2. Ta bort CacheWorker.c/h
3. Uppdatera GridGuard.c - 3 threads istället för 4
```

**Filer som påverkas:**
- Nya: `src/application/services/CacheDB.c/h`
- Ändringar: `src/application/workers/FetchWorker.c`
- Ändringar: `src/application/workers/ComputeWorker.c`
- Tas bort: `src/application/workers/CacheWorker.c/h`
- Tas bort: `src/application/services/Cache.c/h`

**Vem kan göra:** 2-3 personer tillsammans (många filer påverkas)

**Varför viktigt:** Cache-databasen kan delas mellan watchdog-restarts och flera server-instanser

---

### 4. IPC med pipes/shared memory (Medium prioritet, kurs-demo)

**Uppgift:** Lägg till IPC-exempel för att demonstrera kursmål

**Alternativ A: Shared memory för cache**
```c
// concurrency/ipc/SharedCache.c/h
int shm_fd = shm_open("/gridguard_cache", O_CREAT | O_RDWR, 0666);
ftruncate(shm_fd, sizeof(SharedCacheData));
SharedCacheData *cache = mmap(NULL, sizeof(SharedCacheData),
                              PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

// Synkronisera med semaforer
sem_t *sem = sem_open("/gridguard_cache_sem", O_CREAT, 0666, 1);
```

**Alternativ B: Named pipe för heartbeat**
```c
// infrastructure/watchdog/Heartbeat.c
mkfifo("/tmp/gridguard_heartbeat", 0666);

// Daemon skriver heartbeat
write(heartbeat_fd, "ALIVE", 5);

// Watchdog läser heartbeat
read(heartbeat_fd, buffer, sizeof(buffer));
```

**Vem kan göra:** 1 person, efter daemon/watchdog är klart

---

### 5. Dokumentation och tester (Låg prioritet men viktigt)

**Tester som behövs:**
- `tests/unit/test_config_loader.c` - Testa JSON-parsing av user configs
- `tests/unit/test_cache_db.c` - Testa SQLite cache operations
- `tests/integration/test_daemon.c` - Testa daemonize-processen
- `tests/integration/test_watchdog.c` - Testa restart-logik

**Dokumentation:**
- Uppdatera README.md med nya features
- Skriv DATAHANDLING.md med beslut om JSON/SQLite
- Dokumentera user config JSON-schema

---

## Arbetsfördelning - Förslag

### Person/Team 1: User configs & JSON
**Uppgifter:**
- Definiera UserConfig struct
- Implementera ConfigLoader med cJSON
- Skapa exempel user config JSON-filer
- Uppdatera ClientHandler för att läsa userId

**Tidsåtgång:** ~4-6 timmar
**Svårighetsgrad:** Medel

---

### Person/Team 2: Daemon & Watchdog
**Uppgifter:**
- Implementera Daemon.c/h (daemonize, PID-fil, signals)
- Implementera Watchdog.c/h (fork, exec, waitpid, restart)
- Uppdatera main.c med -d flagga
- Skriva tester och test-scenarion

**Tidsåtgång:** ~8-10 timmar
**Svårighetsgrad:** Hög (kräver förståelse för processer)
**Rekommendation:** Gör tillsammans, screenshare

---

### Person/Team 3: SQLite cache
**Uppgifter:**
- Skapa CacheDB wrapper runt SQLite
- Definiera databas-schema
- Integrera med FetchWorker
- Ta bort gamla Cache.c och CacheWorker.c
- Uppdatera GridGuard.c (3 threads)

**Tidsåtgång:** ~6-8 timmar
**Svårighetsgrad:** Medel-Hög
**Rekommendation:** Gör tillsammans (många filer påverkas)

---

### Person/Team 4: IPC-demo (optional)
**Uppgifter:**
- Implementera shared memory ELLER named pipe
- Dokumentera IPC-användning för kursen
- Skriva tester

**Tidsåtgång:** ~3-4 timmar
**Svårighetsgrad:** Medel
**När:** Efter daemon/watchdog är klart

---

### Alla tillsammans
**Uppgifter:**
- Code review när varje del är klar
- Integration testing
- Uppdatera dokumentation
- Förbered presentation/examination

---

## Tekniska detaljer

### Bygga och testa

```bash
# Bygg allt
make clean && make

# Tester
make test-logger      # Logger (unit)
make test-api         # API fetch (integration)
make test-pipeline    # Pipeline (integration)

# Kör server (normal mode)
./bin/GridGuard-server

# Kör server (daemon mode - efter implementation)
./bin/GridGuard-server -d

# Kör watchdog (efter implementation)
./bin/GridGuard-watchdog
```

### Git workflow

**Feature branches:**
```bash
git checkout -b feature/user-configs
git checkout -b feature/daemon-watchdog
git checkout -b feature/sqlite-cache
```

**Merge till development när klar:**
```bash
git checkout development
git merge feature/user-configs
git push origin development
```

---

## Sammanfattning av dagen

**Genomfört:**
✅ Komplett omstrukturering av src/ med lagerbaserad arkitektur
✅ infrastructure/, network/, configs-mappar skapade
✅ Dokumentation: DAEMON_WATCHDOG.md (380+ rader)
✅ Dokumentation: PROJECT_STRUCTURE.md uppdaterad
✅ Makefile uppdaterad för nya strukturen
✅ Bygg-verifierat - allt kompilerar

**Analyserat:**
✅ Datahantering (JSON vs SQLite vs HDF5)
✅ Cache-strategi och persistent storage
✅ User-specific configs istället för hårdkodad data

**Planerat:**
✅ Daemon & watchdog implementation
✅ SQLite-baserad cache
✅ User config-system
✅ IPC-exempel för kursen

**Förberett:**
✅ Tydlig arbetsfördelning
✅ Konkreta implementationssteg
✅ Test-planer

---

## Prioriteringar

### Måste göras (för att systemet ska fungera riktigt)
1. **User configs** - Ta bort hårdkodad data
2. **Cache-refaktorering** - Cacha rätt data (weather/prices, inte energiplaner)
3. **Daemon/Watchdog** - Täcker kursmål om processer och IPC

### Bör göras (ökar kvalitet)
4. SQLite för persistent cache
5. Tester för nya komponenter
6. IPC-exempel (shared memory eller pipes)

### Kan göras (nice-to-have)
7. Historisk datalogging
8. Export till CSV
9. Doxygen-dokumentation

---

## Nästa session - Checklista

**Innan vi börjar:**
- [ ] Läs DAEMON_WATCHDOG.md
- [ ] Bestäm vem som gör vad
- [ ] Skapa feature branches för respektive team, t ex om Powell/Alexander tar på sig att göra SQL LITE cache så skapar Powell eller Alexander en ny branch som heter feature/sql-lite-cache och sen när dom är klar med den och innan den körs in till dev så kör vi en gemensam code review för att gemensamt se till så att allting är som det ska.

**Under session:**
- [ ] Implementera user configs (Team 1)
- [ ] Implementera daemon/watchdog (Team 2)
- [ ] Implementera SQLite cache (Team 3)
- [ ] IPC-demo om tid finns (Team 4)

**Efter implementation:**
- [ ] Code review tillsammans
- [ ] Integration testing
- [ ] Uppdatera dokumentation
- [ ] Commit till development
- [ ] Skriv nästa CHANGELOG

