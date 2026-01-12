# LEOP Projektstruktur

Detta dokument beskriver projektets mappstruktur och syfte med varje komponent.

## Övergripande struktur

```
leop/
├── src/                    # Källkod
│   ├── server/            # C-baserad server
│   ├── client/            # C++-baserad klient
│   ├── common/            # Delad kod mellan server och klient
│   └── tests/             # Enhetstester och integrationstester
│
├── docs/                   # Dokumentation
│   ├── ARCHITECTURE.md    # Arkitekturdokumentation
│   ├── API.md            # API-dokumentation
│   └── PROFILING.md      # Profileringsrapporter
│
├── config/                 # Konfigurationsfiler
│   ├── server.conf.example
│   └── client.conf.example
│
├── scripts/                # Build och deployment scripts
│   ├── setup.sh           # Utvecklingsmiljö setup
│   └── run_tests.sh       # Test runner
│
├── build/                  # Kompilerade objektfiler (git-ignorerad)
├── bin/                    # Executables (git-ignorerad)
├── logs/                   # Loggfiler (git-ignorerad)
│
├── Makefile               # Build-system
├── README.md              # Projektöversikt
├── BACKLOG.md             # Projektbacklog
├── CONTRIBUTING.md        # Bidragsriktlinjer
└── .gitignore             # Git ignore-filer
```

## Detaljerad beskrivning

### `/src` - Källkod

#### `/src/server` - C Server
Serverkomponenter skrivna i C:

```
src/server/
├── main.c                 # Entry point, main loop
├── server.c/h             # Server-logik, socket handling
├── connection.c/h         # Klientanslutningar
├── config.c/h             # Konfigurationshantering
├── logger.c/h             # Loggningssystem
├── fetch.c/h              # Datahämtning från API
├── parse.c/h              # JSON-parsing
├── compute.c/h            # Energiberäkningar
├── cache.c/h              # Cache-implementation
└── protocol.c/h           # Kommunikationsprotokoll
```

#### `/src/client` - C++ Klient
Klientkomponenter skrivna i C++:

```
src/client/
├── main.cpp               # Entry point
├── Client.cpp/hpp         # Huvudsaklig klient-klass
├── Connection.cpp/hpp     # Socket-anslutning
├── CommandParser.cpp/hpp  # CLI argument parsing
└── Display.cpp/hpp        # Output formattering
```

#### `/src/common` - Gemensam kod
Kod som delas mellan server och klient:

```
src/common/
├── types.h                # Gemensamma datatyper
├── protocol.h             # Protokolldefinitioner
├── utils.c/h              # Hjälpfunktioner
└── constants.h            # Konstanter
```

#### `/src/tests` - Tester
Enhetstester och integrationstester:

```
src/tests/
├── test_cache.c           # Cache-tester
├── test_parser.c          # Parser-tester
├── test_compute.c         # Beräkningstester
├── test_integration.c     # End-to-end tester
└── test_runner.c          # Test framework
```

### `/docs` - Dokumentation

```
docs/
├── ARCHITECTURE.md        # Systemarkitektur
├── API.md                 # API-specifikation
├── PROFILING.md           # Profileringsresultat
├── SETUP.md               # Setup-instruktioner
└── images/                # Diagram och bilder
    ├── architecture.png
    └── pipeline.png
```

### `/config` - Konfiguration

```
config/
├── server.conf.example    # Exempel på server-config
├── client.conf.example    # Exempel på klient-config
├── development.conf       # Dev-miljö settings
└── production.conf        # Prod-miljö settings
```

### `/scripts` - Automation

```
scripts/
├── setup.sh               # Setup utvecklingsmiljö
├── run_tests.sh           # Kör alla tester
├── deploy.sh              # Deployment script
└── benchmark.sh           # Performance benchmarks
```

## Filnamngivning

### C-filer
- **Header files:** `.h` extension
- **Implementation:** `.c` extension
- **Naming:** `snake_case`
- **Example:** `energy_optimizer.c`, `energy_optimizer.h`

### C++-filer
- **Header files:** `.hpp` extension (eller `.h`)
- **Implementation:** `.cpp` extension
- **Naming:** `PascalCase` för klasser, `snake_case` för filer
- **Example:** `EnergyOptimizer.cpp`, `EnergyOptimizer.hpp`

### Dokumentation
- **Markdown:** `.md` extension
- **Naming:** `UPPERCASE` för viktiga docs, `lowercase` för övriga
- **Example:** `README.md`, `ARCHITECTURE.md`, `guide.md`

## Build Artifacts

Följande genereras under build och ska inte committas:

```
build/                     # Objektfiler
bin/                       # Executables
logs/                      # Loggfiler
*.o                        # Objektfiler
*.so, *.a                  # Libraries
gmon.out                   # Profiling data
*.gcda, *.gcno             # Coverage data
```

## Konfigurationsfiler

### server.conf
Innehåller:
- Server port och socket path
- API endpoints och nycklar
- Cache settings
- Log-konfiguration

### client.conf
Innehåller:
- Server connection details
- Output format preferences
- Timeout settings

## Loggar

```
logs/
├── server.log             # Server-loggar
├── client.log             # Klient-loggar
├── error.log              # Endast fel
└── debug.log              # Debug-information
```

## Hur man lägger till ny funktionalitet

1. **Skapa nya filer i rätt katalog**
   ```bash
   # För server-funktionalitet
   touch src/server/new_feature.c
   touch src/server/new_feature.h
   
   # För klient-funktionalitet
   touch src/client/NewFeature.cpp
   touch src/client/NewFeature.hpp
   ```

2. **Uppdatera Makefile om nödvändigt**
   - Lägg till nya källfiler i `*_SRCS` variabler
   - Lägg till nya dependencies om det behövs

3. **Dokumentera**
   - Uppdatera relevant dokumentation
   - Lägg till kommentarer i koden

4. **Testa**
   - Skapa tester i `/src/tests`
   - Uppdatera `test_runner.c`

## Git Workflow

1. **Feature branch**
   ```bash
   git checkout -b feature/new-functionality
   ```

2. **Commit changes**
   ```bash
   git add src/server/new_feature.c
   git commit -m "feat(server): add new functionality"
   ```

3. **Push och skapa PR**
   ```bash
   git push origin feature/new-functionality
   ```

## Nästa steg för vecka 1

- [ ] Skapa grundläggande mappstruktur
- [ ] Skapa placeholder-filer för viktiga komponenter
- [ ] Implementera grundläggande build-system
- [ ] Sätt upp Git-repository
- [ ] Skapa exempel-konfigurationsfiler

---

**Uppdaterad:** 2025-01-XX  
**Team:** [TEAM-NAMN]
