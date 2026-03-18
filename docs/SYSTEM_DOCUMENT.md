# GridGuard — Systemdokumentation

**Local Energy Optimization Platform (LEOP)**
Kurs 3 — Systemutvecklare C/C++, Chas Academy

---

## 1. Vad systemet försöker åstadkomma

GridGuard är ett lokalt körbart system för energioptimering. Målet är att hjälpa en fastighet — ett hem, en arena eller liknande — att köpa el från nätet när det är billigt och använda eller sälja sin solenergi när det lönar sig som mest.

Systemet kombinerar två datakällor i realtid:

- **Väderdata** (solinstrålning, temperatur, molnighet) från OpenMeteo API
- **Spotprisdata** (15-minutersintervall, SE1–SE4) från Elpriset API

Utifrån detta beräknas en energiplan för de kommande 24–48 timmarna med rekommendationer per 15-minutersslot: **BUY** (köp el från nätet nu), **SELL** (sälj överskottsproduktion), **IDLE** (neutralt läge) eller **SKIP** (undvik förbrukning).

---

## 2. Input och output

**Input:**
- Realtidsväderdata från OpenMeteo (var 15:e minut)
- Spotprisdata från Elpriset (var 15:e minut)
- Användarkonfiguration: solpanelstorlek (m²), verkningsgrad, förbrukningsprofil, geografisk position

**Output:**
- JSON-API på port 8080 med `/forecast`, `/schedule`, `/metrics`, `/health`
- CLI-dashboard (C++-klient) med färgkodade BUY/SKIP-signaler, prisgrafer och schemaläggning av flexibla laster
- Automatisk schemaläggning — systemet kan lägga in "kör tvättmaskinen kl 03:30" baserat på lägsta pris inom en deadline

---

## 3. Processarkitektur

Systemet körs som fyra separata processer under en processövervakare:

```
GridGuard (launcher)
└── GridGuard-watchdog   ← processövervakare, rotprocess
    ├── GridGuard-fetcher ← hämtar extern data
    ├── GridGuard-parser  ← validerar och strukturerar data
    └── GridGuard-server  ← HTTP-server + energiberäkning
            └── [tråd] ComputeWorker
```

### GridGuard-watchdog
Watchdog är systemets rotprocess och ansvarar för hela processträdet. Den:
- Skapar alla IPC-resurser (FIFOs, Unix socket) **innan** child-processerna startas
- Startar processerna i rätt ordning (parser → fetcher → server)
- Övervakar varje process via **heartbeat-pipes** (anonyma pipes som arvas vid fork)
- Startar om hela processgruppen vid krasj, med exponentiell backoff (2s → 4s → 8s → 16s → 32s)
- Skriver processtatistik till POSIX shared memory som servern exponerar via `/metrics`

**Hur watchdog vet att en process lever:** Varje child-process skriver periodiskt `"hb"` till sin heartbeat-pipe. Watchdog pollar pipe:n var 2:a sekund med `poll()`. Om ingen heartbeat kommit inom timeout klassas processen som fryst och hela gruppen startas om.

**Restart policy:** Max 5 omstarter per 300-sekunders fönster. Hela gruppen startas alltid om — inte bara den kraschade processen — eftersom processerna är sammankopplade via FIFOs och ett krasj i Fetcher ger ett hängt tillstånd i Parser ändå.

### GridGuard-fetcher
Tar emot en `WorkRequest` från servern, gör HTTPS-anrop mot OpenMeteo och Elpriset, och skickar rådata vidare som en `FetchResult` till parsern. Cachelagrar resultaten i POSIX shared memory för att undvika onödiga API-anrop.

### GridGuard-parser
Tar emot `FetchResult`, validerar JSON-svaren, konverterar tidsstämplar till Unix-tid, matchar väder- och prisdata per 15-minutersslot och skickar en strukturerad `ParseResult` till ComputeWorker.

### GridGuard-server + ComputeWorker
Servern hanterar inkommande HTTP-anslutningar med en trådpool. ComputeWorker är en dedikerad tråd som tar emot `ParseResult` via Unix domain socket, kör optimeringsalgoritmen och skriver resultatet till shared memory-cachen. HTTP-tråden läser cachen och returnerar JSON till klienten.

---

## 4. IPC — kommunikation mellan processer

```
Server ──[FIFO]──► Fetcher      WorkRequest  (struct, binär)
Fetcher ──[FIFO]──► Parser      FetchResult  (struct, binär)
Parser ──[Unix socket]──► ComputeWorker   ParseResult  (struct, binär)
Alla processer ◄──► SharedCache  POSIX shared memory (väder, pris, prognos)
Watchdog ──► SharedCache        WatchdogMetrics (processstatistik)
```

**Varför namngivna FIFOs och inte anonyma pipes?**
Watchdog startar processerna med `fork() + exec()`. De ersätter sig själva med nya binärer via exec och ärver inte file descriptors på ett kontrollerbart sätt. Namngivna FIFOs i filsystemet gör att processerna hittar varandra utan att behöva dela fd:er via arv.

**Varför Unix domain socket mot ComputeWorker?**
ComputeWorker är en tråd inuti serverprocessen — inte en separat process. Unix socket hanterar överföringen av stora `ParseResult`-structs på ett tillförlitligt sätt och ger naturlig flödeskontroll, jämfört med en FIFO som saknar meddelandegränser.

**Varför POSIX shared memory för cachen?**
Fetcher, Parser och Server behöver alla läsa väder- och prisdata. Shared memory eliminerar kopiering — alla processer mappar samma minnessida. Åtkomst skyddas med `pthread_rwlock` (många läsare, en skrivare).

---

## 5. C++-klienten

Klienten är skriven i C++17 och kommunicerar med servern via HTTP. Den demonstrerar kursmomentens C++-krav:

- **RAII:** `SocketGuard` wrapprar POSIX file descriptors med Rule of Five (destruktor, move-konstruktor, move-tilldelning, delete på copy)
- **STL:** `std::vector`, `std::string`, `std::unique_ptr`, `std::sort`, `std::partial_sort`, `std::accumulate`
- **Exception handling:** Custom exceptions (`ConnectionError`, `NetworkError`) med `try/catch`
- **Namespaces:** All klientkod under `namespace gridguard`

---

## 6. Profileringsresultat

Profilering utfördes med `clock_gettime(CLOCK_MONOTONIC)` (10 000 iterationer), `gprof` och `valgrind --leak-check=full`.

### Hotspot

`gprof` visar att 100 % av CPU-tid i compute-steget ligger i en enda funktion: `Compute_GenerateEnergyPlan()`. Övriga funktioner (Logger, init) är negligibla.

### Benchmark: -O0 vs -O2 (Compute_GenerateEnergyPlan, 96h prognos)

| | avg (-O0) | avg (-O2) | Förbättring | p99 (-O0) | p99 (-O2) | Förbättring |
|--|--|--|--|--|--|--|
| Realistisk prognos | 3.22 µs | 2.10 µs | **1.53×** | 29.51 µs | 8.38 µs | **3.52×** |
| Worst-case | 2.27 µs | 1.56 µs | **1.45×** | 23.87 µs | 3.89 µs | **6.14×** |
| Stor solpanel | 2.40 µs | 1.57 µs | **1.53×** | 6.95 µs | 4.69 µs | **1.48×** |

Genomströmning med `-O2`: ~477 000 planer/sekund. Systemet beräknar en ny plan var 15:e minut — marginalen är på ordningen 7 000 000×.

### Minnesanalys (Valgrind)

Inga heap-läckor. En oinitialiserad stackvariabel hittades i benchmark-koden (inte produktionskoden) och dokumenterades.

### Analys

Compute-steget är inte en flaskhals för det nuvarande användningsfallet. Den primära orsaken till p99-spikarna (8 µs vs p50 på 1.6 µs) är OS-schemaläggningsavbrott, inte kod — detta är inte åtgärdbart utan en realtidskärna.

---

## 7. Förbättringsmöjligheter

**Baserat på profileringen:**

| Prioritet | Komponent | Problem | Åtgärd |
|-----------|-----------|---------|--------|
| Låg | Compute | `qsort` över hela prisarrayen vid köpfönsterberäkning | Ersätt med `nth_element`-ekvivalent — O(N) istället för O(N log N) |
| Låg | Cache | Linjär sökning O(N) vid lookup (N=16) | Hash-tabell om N skalas upp |
| Låg | Queue | Mutex-contention vid 4 producenter / 1 konsument | Batch-dequeue för att minska lock-frekvens |

Samtliga förbättringar är mikrooptimeringar — systemet är redan i princip så effektivt det behöver vara för sitt syfte.

**Arkitekturella förbättringar om mer tid funnits:**

- **Batterimodell:** Systemet simulerar inte batteriladdningscykler eller degradering. En riktig batterimodell skulle ge mer precisa köp/sälj-beslut.
- **Historikbaserad prognos:** Energiplanen baseras enbart på aktuell prognos. Med historisk förbrukningsdata och maskininlärning skulle förutsägelserna bli mer precisa.
- **Konfigurationsfil i repo:** INI-parsern är implementerad men ingen standardkonfigurationsfil medföljer i repot — deployment kräver manuell setup av miljövariabler.
- **Makefile dependency tracking (`-MMD`):** Utan detta kan stale `.o`-filer efter headerändringar ge svårspårade fel. Vi brände oss på det under utvecklingen och förlitade oss på `make clean` som workaround.
