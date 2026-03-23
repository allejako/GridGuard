# GridGuard — Systemdokumentation

**Local Energy Optimization Platform (LEOP)**
Kurs 3 — Systemutvecklare C/C++, Chas Academy

---

## 1. Vad vi försökte åstadkomma

GridGuard är en lokal energioptimerings-plattform för fastigheter med solceller. Målet var att bygga ett system som i realtid analyserar väderprognos och spotprisdata och ger konkreta rekommendationer per 15-minutersslot: **köp** el nu, **sälj** solöverskott, **undvik** förbrukning, eller **avvakta**.

**Demokund: SAAB Arena Linköping**

SAAB Arena är vår referenskund — en multiarena med:
- 500 m² solpaneler på taket
- 50 kWh basförbrukning per 15 min (200 kW konstant drift)
- Elbilsflotta: 10 platser × 11 kW laddning
- Zamboni (ismaskin): 15 kW, måste köras innan match
- HVAC: 50 kW förvärmning inför event

**Problemet:** Utan optimering laddas bilarna när de anländer (toppbelastningstider), solenergi exporteras oavsett pris, och tunga laster körs utan hänsyn till spotpriser. GridGuard löser detta med automatisk schemaläggning.

**Resultat med GridGuard:**

| Mätpunkt | Före | Efter | Förbättring |
|----------|------|-------|-------------|
| Elkostnad/dag | 5 200 kr | 3 568 kr | **-31%** |
| Elbilsladdning | 900 kr | 320 kr | **-64%** |
| Solenergi-intäkt | 0 kr | ~450 kr/mån | **+450 kr** |

ROI för arenainstallationen: **~1.2 månader**

---

## 2. Input och Output

**Input:**
- Väderprognos (solinstrålning, temperatur, vind, molnighet) från Open-Meteo API — uppdateras vid cache-miss, TTL 15 min
- Spotpriser per 15-minutersslot för SE1–SE4 från Elprisetjustnu — publiceras dagligen kl 13:00
- Användarkonfiguration: solpanelsstorlek (m²), verkningsgrad, geografisk position, elområde, förbrukningsprofil, nätavgifter

**Output:**
- 48-timmars energiplan uppdelad i 192 kvartar (var 15:e minut) med signal BUY/SELL/AVOID/IDLE per slot
- Aggregerade actionfönster (sammanhängande block) med genomsnittspris, solproduktion, duration
- Bästa köpfönster (billigaste sammanhängande block för flexibla laster)
- HTTP JSON-API på port 8080: `/forecast`, `/user/config`, `/schedule`, `/health`, `/metrics`
- Terminalbaserat dashboard via C++-klient med färgkodade signaler, prisgrafer och schemaläggning

**Exempel-output (SAAB Arena, solig dag):**
```
BUY   02:00–05:45  0.32 kr/kWh  (-54%)   →  Ladda elbilsflottan nu
SELL  12:00–13:45  1.85 kr/kWh  (+45%)   →  Exportera solöverskott
BUY   13:00–14:15  0.39 kr/kWh  (-32%)   →  Kör zamboni-förvärmning
AVOID 17:00–20:00  2.15 kr/kWh  (+63%)   →  Skjut upp icke-kritiska laster

Total daglig besparing: 640 kr (schemalagda laster)
```

---

## 3. Processarkitektur

Systemet körs som fyra separata processer under en processövervakare:

```
GridGuard (launcher)
└── GridGuard-watchdog      ← processövervakare, rotprocess
    ├── GridGuard-fetcher   ← hämtar extern data via HTTPS
    ├── GridGuard-parser    ← validerar och strukturerar rådata
    └── GridGuard-server    ← HTTP-server + JWT-auth + DB
            └── [tråd] ComputeWorker  ← energiberäkning
```

### GridGuard-watchdog

Watchdog är systemets rotprocess. Den:

1. Skapar alla IPC-resurser (FIFOs, Unix domain socket, shared memory) **innan** child-processerna startas
2. Startar processerna i rätt ordning: parser → fetcher → server
3. Övervakar varje process via **heartbeat-pipes** (anonyma pipes ärvda vid fork)
4. Startar om hela processgruppen vid krasj med **exponentiell backoff**: 2s → 4s → 8s → 16s → 32s
5. Skriver processtatistik till POSIX shared memory som servern exponerar via `/metrics`

**Heartbeat-mekanism:** Varje child-process skriver periodiskt `"hb"` till sin heartbeat-pipe. Watchdog pollar med `poll()` var 2:a sekund. Om ingen heartbeat kommit inom 15 sekunder klassas processen som fryst och hela gruppen startas om.

**Restart policy:** Max 5 omstarter per 300-sekunders fönster. Hela gruppen startas alltid om — inte bara den kraschade processen — eftersom ett krasj i Fetcher ger ett hängt tillstånd i Parser ändå (FIFOn hänger utan producer).

**Watchdog-moduler** (uppdelade från ursprunglig monolitisk fil på 441 rader):
- `WatchdogSignals.c` — sigaction, signal_handler (async-signal-safe isolation)
- `Heartbeat.c` — heartbeat-pipe management med opak pekare
- `RestartPolicy.c` — restart tracking och backoff (konfigurationsbar, testbar isolerat)
- `Watchdog.c` — spawn, status FIFO, main loop (220 rader)

### GridGuard-fetcher

Tar emot en `WorkRequest` via anonym pipe, gör HTTPS-anrop mot Open-Meteo och Elprisetjustnu med mbedTLS, och skickar rådata vidare som `FetchResult` via named FIFO. Kontrollerar POSIX shared memory-cache innan API-anrop (TTL-baserad).

### GridGuard-parser

Tar emot `FetchResult` via named FIFO, validerar JSON-svaren med cJSON, konverterar tidsstämplar till Unix-tid, matchar väder- och prisdata per 15-minutersslot och skickar strukturerad `ParseResult` till ComputeWorker via Unix domain socket. Skapar notify-FIFO för att väcka ComputeWorker asynkront.

### GridGuard-server + ComputeWorker

Servern hanterar inkommande HTTP-anslutningar med en trådpool. JWT-validering sker med mbedTLS (HS256). ComputeWorker är en dedikerad tråd som tar emot `ParseResult` via Unix domain socket, kör optimeringsalgoritmen och skriver resultatet till shared memory-cachen. HTTP-tråden läser cachen och returnerar JSON till klienten.

---

## 4. IPC — Kommunikation mellan processer

```
Server  ──[anonym pipe]──►  Fetcher      WorkRequest   (struct, binär, ~200 B)
Fetcher ──[named FIFO]──►   Parser       FetchResult   (struct, binär, ~120 kB)
Parser  ──[Unix socket]──►  ComputeWorker ParseResult  (struct, binär, ~85 kB)
Alla    ◄──► SharedCache    POSIX shared memory        (väder, pris, prognos)
Watchdog ──► SharedCache    WatchdogMetrics            (processstatistik)
```

| Kanal | Teknik | Varför |
|-------|--------|--------|
| Server → Fetcher | Anonym pipe | Liten WorkRequest-struct; server är parent till fetcher |
| Fetcher → Parser | Named FIFO | Processerna kör via exec och ärver inte fd:er — FIFO i filsystemet löser det |
| Parser → ComputeWorker | Unix domain socket | ComputeWorker är tråd, inte process; socket ger meddelandegränser för stora structs |
| Cache | POSIX shared memory | Tre processer behöver läsa samma data — shared memory eliminerar kopiering |

**Cache-synkronisering:** `pthread_rwlock` — många läsare (HTTP-trådar), en skrivare (ComputeWorker). Väder- och prisdata delas mellan processer via `shm_open` + `mmap`.

**Varför hela gruppen startas om vid krasj?** Named FIFOs och Unix sockets hänger i öppet läge tills båda sidor stängs. Om Fetcher kraschar blockerar Parser på `read()` för alltid. Att starta om bara Fetcher skulle kräva att Parser detekterar EOF och återansluter — komplexitet som inte är värd det för ett single-node system.

---

## 5. Energialgoritmen

### Solcellsmodell (IEC-baserad)

1. **Paneltemperatur** (NOCT-modell, IEC 61215):
   `panelTemp = luftTemp + (solinstrålning × 0.03125) / (1 + 0.04 × vind)`

2. **Temperaturderating** (IEC 61724):
   `tempEff = 1.0 + (-0.0045 per °C) × (panelTemp - 25°C)`
   → Vid 60°C: 16% effektförlust

3. **Energiproduktion per kvart:**
   `produktion = (W/m² / 1000) × areaM² × verkningsgrad × systemförluster × tempEff × 0.25h`

Systemförluster (kablar, växelriktare) är satta till 25%.

### Beslutlogik

Algoritmen beräknar P33 och P70 (33:e och 70:e percentilen) av spotpriserna för hela prognoshorisonten.

| Signal | Villkor |
|--------|---------|
| **BUY** | Totalkostnad ≤ P33 — billigaste tredjedelen |
| **AVOID** | Totalkostnad ≥ P70 OCH inget solöverskott |
| **SELL (premium)** | Solöverskott > 0.5 kWh OCH pris ≥ P70 OCH spotpris ≥ MIN_PRICE_TO_SELL |
| **SELL (surplus)** | Solöverskott > 0.5 kWh OCH spotpris ≥ MIN_PRICE_TO_SELL (mid-range) |
| **IDLE** | Allt annat |

`MIN_PRICE_TO_SELL_SEK` (0.01 kr/kWh) förhindrar export vid negativa spotpriser — ett bug-fix som adderades efter analys av edge cases.

### Smart schemaläggning (LoadScheduler)

`POST /schedule` tar emot `load_id`, `duration_minutes` och `power_kw`. LoadScheduler söker igenom alla BUY-signaler kommande 48 timmar, hittar sammanhängande block som ryms inom durationen, viktar med "practicality score" (natt 1.0×, dag 0.5×, kväll 1.5×) och väljer fönstret med lägst viktad kostnad.

---

## 6. C++-klienten

Klienten är skriven i C++17 och demonstrerar kursmomentens C++-krav:

- **RAII:** `SocketGuard` — wrapprar POSIX file descriptors med Rule of Five (destruktor, move-konstruktor, move-tilldelning, deleted copy)
- **STL:** `std::vector`, `std::string`, `std::unique_ptr`, `std::sort`, `std::accumulate`
- **Exception handling:** Custom exceptions (`ConnectionError`, `NetworkError`) med `try/catch`-hierarki
- **Namespaces:** All klientkod under `namespace gridguard`

**Kommandon:**
```bash
bin/GridGuard-client --token $TOKEN health
bin/GridGuard-client --token $TOKEN forecast [--watch --interval 60]
bin/GridGuard-client --token $TOKEN config set --solar-area 500 --solar-eff 0.20
bin/GridGuard-client --token $TOKEN schedule add --load ev_fleet --duration 240 --power 110
```

**Dashboard-output (terminal):**
```
GridGuard                          13.1°C  Sol: 90.57 kW
SAAB_ARENA  SE3  Linköping

▸ 03-21 02:00  BUY   0.32 kr  ░░░░░░░░░░   90 kWh   -54%
▸ 03-21 12:00  SELL  1.85 kr  █████████░   12 kWh   +45%
▸ 03-21 17:00  AVOID 2.15 kr  ██████████    4 kWh   +63%

🥇 02:00  0.32 kr/kWh  (-54%)
🥈 13:00  0.39 kr/kWh  (-32%)

Import: 1612 kWh   Kostnad: 3568 kr   Bästa köp: 02:00
```

---

## 7. Autentisering och Privacy

**JWT (HS256, 24h TTL)** — stateless autentisering utan att servern behöver kontakta plattformen vid varje request. Platform-servern utfärdar tokens från `platform.db`; servern validerar signaturen med mbedTLS.

**Privacy-by-architecture:** Känslig data lämnar aldrig enheten.

| Data | Plattform (moln) | GridGuard (enhet) |
|------|-----------------|-------------------|
| userId | ✅ | ✅ |
| GPS-koordinater | ❌ | ✅ |
| Solpanelsstorlek | ❌ | ✅ |
| Förbrukningsprofil | ❌ | ✅ |

En kompromiss av platform.db exponerar **inga** energimönster.

---

## 8. Profileringsresultat

Profilering utfördes med `clock_gettime(CLOCK_MONOTONIC)` (10 000 iterationer), `gprof` och `valgrind --leak-check=full`.

**gprof:** 100% av compute-CPU-tid ligger i `Compute_GenerateEnergyPlan()`. Övriga funktioner (Logger, init) är negligibla.

### Benchmark: -O0 vs -O2 (Compute_GenerateEnergyPlan, 96h prognos)

| Scenario | avg -O0 | avg -O2 | Förbättring | p99 -O0 | p99 -O2 | Förbättring |
|----------|---------|---------|-------------|---------|---------|-------------|
| Realistisk | 3.22 µs | 2.10 µs | **1.53×** | 29.51 µs | 8.38 µs | **3.52×** |
| Worst-case | 2.27 µs | 1.56 µs | **1.45×** | 23.87 µs | 3.89 µs | **6.14×** |
| Stor solpanel | 2.40 µs | 1.57 µs | **1.53×** | 6.95 µs | 4.69 µs | **1.48×** |

**Genomströmning med -O2:** ~477 000 planer/sekund. Systemet beräknar en ny plan var 15:e minut — marginalen är på ordningen **7 000 000×**.

**Minnesanalys (Valgrind):** Inga heap-läckor. En oinitialiserad stackvariabel hittades i benchmark-koden (inte produktionskoden) och dokumenterades.

**Analys:** Compute-steget är inte en flaskhals. p99-spikarna (8 µs vs p50 1.6 µs) beror på OS-schemaläggningsavbrott — inte kod. Detta är inte åtgärdbart utan en realtidskärna.

---

## 9. Förbättringsmöjligheter

### Kodkvalitet (baserat på profilering)

| Prioritet | Komponent | Problem | Åtgärd |
|-----------|-----------|---------|--------|
| Låg | Compute | `qsort` för hela prisarrayen vid köpfönsterberäkning | `nth_element`-ekvivalent — O(N) vs O(N log N) |
| Låg | SharedCache | Linjär sökning O(N) vid lookup (N=16) | Hash-tabell om N skalas upp |
| Låg | Queue | Mutex-contention vid 4 producenter/1 konsument | Batch-dequeue för lägre lock-frekvens |

Alla förbättringar är mikrooptimeringar — systemet är redan tillräckligt effektivt.

### Arkitektur och features

| Område | Förbättring |
|--------|-------------|
| **Batterimodell** | Systemet saknar simulering av laddningscykler och degradering. En riktig batterimodell ger mer precisa köp/sälj-beslut (exkluderades medvetet från MVP) |
| **Historikbaserad prognos** | Planen baseras enbart på aktuell prognos. Med historisk förbrukningsdata och ML skulle förutsägelserna bli mer precisa |
| **Makefile -MMD** | Utan dependency tracking kan stale `.o`-filer efter headerändringar ge svårspårade fel. Vi brände oss på det och förlitade oss på `make clean` |
| **Konfigurationsfil** | INI-parsern är implementerad men ingen standardkonfigurationsfil medföljer — deployment kräver manuell setup av miljövariabler |
| **Systemd service** | Systemet startas via `make start`/`make stop`. En systemd unit-fil saknas för produktionsdrift |

### Om vi hade haft mer tid

1. **Batterimodell** — Det mest värdehöjande tillägget. Med batteri kan systemet lagra billig el och sälja/använda den vid höga priser, inte bara reagera på solöverskott.
2. **Enhetstester för Watchdog-moduler** — `RestartPolicy` och `Heartbeat` är nu testbara isolerat men saknar tester. En `RestartPolicy_Create(3, 5, 1)` i test kan verifiera backoff-sekvensen (1→2→4s) utan att vänta på timeout i produktion.
3. **Historisk förbrukningsdata** — GridGuard lagrar varje energiplan i `schedules`-tabellen. Med tillräcklig historik kan en enkel modell förutsäga typisk förbrukning per tid på dygnet och veckodag.
4. **Systemd-integration** — `WatchdogMetrics` skriver redan processstatistik till shared memory. Att koppla det till `systemd-notify` och `sd_watchdog_enabled()` vore ett litet men produktionsrelevant steg.

---

## 10. Bygga och köra

```bash
# Installera beroenden (Ubuntu/Debian)
sudo apt-get install libmbedtls-dev libsqlite3-dev libssl-dev

# Bygg allt och starta med demo-data
make dev        # Seedar databaser, genererar JWT-token, startar watchdog

# Kör tester
make test       # Alla enhetstester
make test-client # Integrationstest för C++-klienten

# Produktion
make clean && make release
make start      # Startar watchdog (hanterar alla processer)
make stop       # Stoppar systemet
```

Binärer: `bin/GridGuard-server`, `bin/GridGuard-fetcher`, `bin/GridGuard-parser`, `bin/GridGuard-watchdog`, `bin/GridGuard-client`
