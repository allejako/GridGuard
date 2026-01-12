# LEOP Projektbacklog

## Sprint-översikt

Detta dokument innehåller en prioriterad lista över funktioner och uppgifter för LEOP-projektet, organiserat per vecka/sprint.

**Legend:**
- 🔴 Kritisk (måste göras)
- 🟡 Viktig (bör göras)
- 🟢 Önskvärd (kan göras om tid finns)
- ⭐ Stretch goal

---

## Vecka 1: Projektintroduktion och Planering

### Setup och Infrastruktur
- [ ] 🔴 Skapa Git-repository
- [ ] 🔴 Sätt upp projektstruktur (mappar och filer)
- [ ] 🔴 Skapa grundläggande Makefile
- [ ] 🔴 Sätt upp .gitignore
- [ ] 🟡 Skapa utvecklingsmiljö-guide
- [ ] 🟡 Sätt upp CI/CD pipeline (GitHub Actions)

### Dokumentation
- [ ] 🔴 README.md med projektöversikt
- [ ] 🔴 ARCHITECTURE.md med systemdesign
- [ ] 🔴 Välj unikt produktnamn
- [ ] 🟡 CONTRIBUTING.md med teamriktlinjer
- [ ] 🟡 API-dokumentation (skelett)
- [ ] 🟢 Skapa projektwiki

### Planering
- [ ] 🔴 Identifiera roller och ansvarsområden
- [ ] 🔴 Skapa detaljerad backlog
- [ ] 🔴 Planera sprintar för vecka 2-12
- [ ] 🟡 Risklista och mitigation-strategier
- [ ] 🟡 Definiera "Definition of Done"

### Presentation (Checkpoint tisdag)
- [ ] 🔴 Förbered offert/lösningsförslag
- [ ] 🔴 Skapa arkitekturdiagram
- [ ] 🔴 Planera demo av projektsetup

---

## Vecka 2: Serverarkitektur och Processhantering

### Server-grundstruktur
- [ ] 🔴 Implementera main.c med event loop
- [ ] 🔴 Kommandoradsargument parsing (port, config)
- [ ] 🔴 Grundläggande loggningssystem
- [ ] 🔴 Konfigurationsfil-läsare
- [ ] 🟡 PID-fil för processhantering

### Processhantering
- [ ] 🔴 Fork-baserad child process hantering
- [ ] 🔴 Signalhantering (SIGINT, SIGTERM, SIGCHLD)
- [ ] 🔴 Korrekt wait()/waitpid() implementation
- [ ] 🟡 Graciös nedstängning av alla processer
- [ ] 🟢 ⭐ Demonisering av server

### Felhantering
- [ ] 🔴 Error logging vid fork-fel
- [ ] 🔴 Resource cleanup vid exit
- [ ] 🟡 Watchdog för hängande processer

### Tester
- [ ] 🔴 Test: Server kan startas och stoppas
- [ ] 🔴 Test: Signalhantering fungerar
- [ ] 🟡 Test: Ingen zombieprocesser

---

## Vecka 3: Multi-threaded Pipeline

### Trådhantering
- [ ] 🔴 Implementera trådpool (minst 3 trådar)
- [ ] 🔴 Fetch-tråd för datahämtning
- [ ] 🔴 Parse-tråd för databearbetning
- [ ] 🔴 Compute-tråd för beräkningar

### Synkronisering
- [ ] 🔴 Mutex-skyddad meddelandekö
- [ ] 🔴 Condition variables för producent-konsument
- [ ] 🔴 Thread-safe loggning
- [ ] 🟡 Read-write locks för cache
- [ ] 🟢 ⭐ Dynamisk trådpool baserad på last

### Pipeline-logik
- [ ] 🔴 Fetch → Parse → Compute dataflöde
- [ ] 🔴 Korrekt resurshantering i varje steg
- [ ] 🟡 Backpressure-hantering vid överbelastning

### Tester
- [ ] 🔴 Test: Pipeline processar data korrekt
- [ ] 🔴 Test: Inga race conditions (Valgrind, Helgrind)
- [ ] 🟡 Load test: Pipeline under hög belastning

---

## Vecka 4: IPC med Pipes

### Pipe-implementation
- [ ] 🔴 Anonyma pipes mellan förälder-barn-processer
- [ ] 🔴 Datahämtare som separat process
- [ ] 🔴 Protokoll för pipe-kommunikation
- [ ] 🟡 Namngivna pipes (FIFO) för flexibilitet
- [ ] 🟢 ⭐ Bidirektionell kommunikation

### Felhantering
- [ ] 🔴 EOF-hantering vid stängd pipe
- [ ] 🔴 SIGPIPE-hantering
- [ ] 🔴 Timeout vid pipe-läsning
- [ ] 🟡 Återanslutningslogik vid fel

### Tester
- [ ] 🔴 Test: Data överförs korrekt via pipes
- [ ] 🔴 Test: Hantering av stängda pipes
- [ ] 🟡 Test: Prestandamätning av pipe-throughput

---

## Vecka 5: Unix Sockets och Delat Minne

### Unix Domain Sockets
- [ ] 🔴 Server-socket för klientanslutningar
- [ ] 🔴 Accept-loop för multipla klienter
- [ ] 🔴 Textbaserat kommunikationsprotokoll (JSON)
- [ ] 🔴 Socket cleanup vid shutdown
- [ ] 🟡 ⭐ Select/poll för multiplexed I/O

### Shared Memory (valfritt men rekommenderat)
- [ ] 🟡 POSIX shared memory för cache
- [ ] 🟡 Semaforer för synkronisering
- [ ] 🟡 Cache-struktur med TTL
- [ ] 🟢 ⭐ LRU cache-strategi

### CLI-klient (initial version)
- [ ] 🔴 Grundläggande C-klient för testning
- [ ] 🔴 Anslutning till Unix socket
- [ ] 🔴 Skicka enkla kommandon

### Tester
- [ ] 🔴 Test: Multipla klienter kan ansluta
- [ ] 🔴 Test: Request/response-cykel fungerar
- [ ] 🟡 Test: Shared memory är thread-safe

---

## Vecka 6: C++-migrering Börjar

### Migration
- [ ] 🔴 Identifiera komponenter för C++-migration
- [ ] 🔴 Konvertera minst en modul till C++
- [ ] 🔴 Ersätt char* med std::string
- [ ] 🟡 Använd references istället för pekare

### C++-features
- [ ] 🔴 Namespaces för organisering
- [ ] 🔴 Const-correctness
- [ ] 🟡 ⭐ Funktionsöverlagring
- [ ] 🟡 ⭐ Default-argument

### Integration
- [ ] 🔴 C och C++ kod samarbetar korrekt
- [ ] 🔴 Extern "C" för gränssnitt
- [ ] 🟡 Build-system stöder både C och C++

---

## Vecka 7: C++ Klasser och Objektdesign

### Klassdesign
- [ ] 🔴 Config-klass för konfiguration
- [ ] 🔴 Cache-klass för datalagring
- [ ] 🔴 EnergyPlan-klass för prognoser
- [ ] 🟡 Logger-klass med singleton-pattern

### OOP-principer
- [ ] 🔴 Konstruktorer och destruktorer
- [ ] 🔴 Public/private inkapsling
- [ ] 🔴 Kopieringskonstruktor och tilldelningsoperator
- [ ] 🟡 ⭐ Move-semantik

### Dokumentation
- [ ] 🔴 Klassdiagram
- [ ] 🔴 API-dokumentation för klasser
- [ ] 🟡 Doxygen-kommentarer

---

## Vecka 8: RAII och Resursförvaltning

### RAII-implementation
- [ ] 🔴 FileDescriptor-klass (RAII wrapper)
- [ ] 🔴 MutexLock-klass (RAII wrapper)
- [ ] 🔴 Rule of Three/Five implementation
- [ ] 🟡 ⭐ Move-semantik med rvalue references

### Exception handling
- [ ] 🔴 Try-catch block för kritiska sektioner
- [ ] 🔴 Custom exception-klasser
- [ ] 🟡 Exception-säkerhet (basic/strong guarantee)
- [ ] 🟡 ⭐ noexcept-specifikation

### Resurshantering
- [ ] 🔴 Eliminera alla manuella delete/free i C++ kod
- [ ] 🔴 Valgrind-test: Inga minnesläckor

---

## Vecka 9: STL-integration

### STL-containers
- [ ] 🔴 std::vector för alla dynamiska arrayer
- [ ] 🔴 std::map eller std::unordered_map för cache
- [ ] 🔴 std::string genomgående
- [ ] 🟡 ⭐ std::optional för nullable värden

### Smart pointers
- [ ] 🔴 std::unique_ptr för ägarskap
- [ ] 🔴 std::shared_ptr där delat ägarskap behövs
- [ ] 🔴 Eliminera alla raw pointers i C++ (där möjligt)

### STL-algoritmer
- [ ] 🔴 Använd minst 3 STL-algoritmer (find, sort, transform, etc.)
- [ ] 🔴 Range-based for-loopar
- [ ] 🟡 Lambda-funktioner med algoritmer

---

## Vecka 10: Profilering och Prestandaanalys

### Profilering
- [ ] 🔴 gprof-profilering av server
- [ ] 🔴 Identifiera top 5 hotspots
- [ ] 🔴 Valgrind minnesanalys
- [ ] 🟡 Cachegrind för cache-analys
- [ ] 🟢 ⭐ Flamegraph-visualisering

### Benchmarking
- [ ] 🔴 Benchmarks för kritiska operationer
- [ ] 🔴 std::chrono för tidstagning
- [ ] 🟡 Jämförelser före/efter optimering

### Dokumentation
- [ ] 🔴 Profileringsrapport med resultat
- [ ] 🔴 Identifierade flaskhalsar
- [ ] 🔴 Optimeringsplan

---

## Vecka 11: Optimering och Färdigställande

### Optimeringar
- [ ] 🔴 Implementera minst 3 optimeringar baserat på profilering
- [ ] 🔴 Kompilatoroptimeringar (-O2, -O3)
- [ ] 🔴 Mätbar prestandaförbättring (före/efter)
- [ ] 🟡 ⭐ Algoritmiska optimeringar
- [ ] 🟢 ⭐ SIMD-optimeringar

### Dokumentation
- [ ] 🔴 Komplett teknisk dokumentation
- [ ] 🔴 API-dokumentation
- [ ] 🔴 Installationsguide
- [ ] 🔴 Användardokumentation
- [ ] 🟡 Doxygen-genererad dokumentation

### Testning
- [ ] 🔴 Komplett test suite
- [ ] 🔴 Integration tests
- [ ] 🟡 Performance regression tests

---

## Vecka 12: Examination

### Förberedelser
- [ ] 🔴 Förbered presentation (15-20 min)
- [ ] 🔴 Live-demo script
- [ ] 🔴 Individuell reflektion (per student)
- [ ] 🔴 Slutgiltig kod-review

### Inlämning
- [ ] 🔴 Komplett källkod i Git
- [ ] 🔴 All dokumentation
- [ ] 🔴 Profileringsrapport
- [ ] 🔴 README med installationsinstruktioner

### Presentation
- [ ] 🔴 System-demo
- [ ] 🔴 Arkitektur-presentation
- [ ] 🔴 Profileringsresultat
- [ ] 🔴 Lärdomar och reflektioner

---

## Stretch Goals (Övergripande)

### Avancerad funktionalitet
- [ ] ⭐ Webbgränssnitt för visualisering
- [ ] ⭐ Databas för historisk data
- [ ] ⭐ Maskininlärningsbaserad prognos
- [ ] ⭐ Batterimodellering med degradering
- [ ] ⭐ Lastsimulering för hushåll

### Prestanda
- [ ] ⭐ SIMD-optimeringar
- [ ] ⭐ Cache-vänlig dataorganisering
- [ ] ⭐ Zero-copy I/O

### Skalbarhet
- [ ] ⭐ Message queues för asynkron kommunikation
- [ ] ⭐ Distribuerad cache
- [ ] ⭐ Load balancing

---

## Risker och Mitigering

| Risk | Sannolikhet | Impact | Mitigering |
|------|-------------|--------|------------|
| API-beroenden fungerar inte | Medium | Hög | Mock API-responses, fallback till cached data |
| Tids brist för alla features | Hög | Medium | Prioritera baskrav, skjut stretch goals |
| Merge conflicts i Git | Medium | Låg | Tydlig branch-strategi, frekventa merges |
| Performance issues | Medium | Medium | Tidig profilering, identifiera bottlenecks |
| Team-medlemmar sjuka | Låg | Hög | Korsutbildning, dokumentation |

---

## Definition of Done

En uppgift anses klar när:
- [ ] Koden är skriven och testad
- [ ] Koden är granskad av minst en teammedlem
- [ ] Tester passerar (enhetstester och integrationstester)
- [ ] Dokumentation är uppdaterad
- [ ] Kod är mergad till develop-branch
- [ ] Valgrind visar inga minnesläckor
- [ ] Kompilering ger inga warnings

---

**Uppdaterad:** [DATUM]  
**Ansvarig:** [PRODUKTÄGARE]
