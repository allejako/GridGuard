# GridGuard — Presentationsmanus
**LEOP · Chas Academy · Kurs 3 Systemutvecklare C/C++**
**Format:** 15–20 minuter · Live-demo · Profileringsresultat · Designdiskussion

---

## BLOCK 1 — Intro (2 min)

> *Stå upp. Börja direkt utan att säga "vi ska prata om..."*

---

**[SÄG:]**

Vi heter [namn] och det här är **GridGuard** — en lokal energioptimerings­plattform.

Problemet vi löser: ett hushåll med solpaneler behöver veta *när* det är lönsamt att köpa el från nätet, *när* det ska ladda batteriet, och *när* det ska sälja överskotts­produktion tillbaka. Det finns redan molntjänster för det — men vi har byggt det som ett lokalt körbart systemnära program, utan molnberoende.

Systemet hämtar väderdata och spotpriser i realtid, beräknar en energiplan på 24–72 timmar, och serverar den via ett HTTP API.

---

## BLOCK 2 — Arkitektur (3 min)

> *Visa arkitekturdiagram — rita på tavlan eller visa bild från docs/images/*

---

**[SÄG:]**

Systemet är byggt som en **multi-process pipeline** i C.

```
Watchdog (supervisor)
├── Fetcher  — hämtar väder + spotprisdata
├── Parser   — validerar och strukturerar rådata
└── Server   — HTTP API + energiberäkningar
               └── [thread] ComputeWorker
```

**Varje process kommunicerar via IPC:**

- Server → Fetcher: **namngiven FIFO** (WorkRequest-struct)
- Fetcher → Parser: **namngiven FIFO** (FetchResult-struct)
- Parser → ComputeWorker: **Unix domain socket** (ParseResult-struct)
- Cache mellan processer: **POSIX shared memory** med `pthread_rwlock` — process-shared

**Watchdog** är supervisorn som startar processerna i rätt ordning, övervakar dem via heartbeat-pipes, och startar om hela gruppen med exponentiell backoff (2s → 4s → 8s → 32s) om något kraschar.

**[BETONA för VG:]**
- Flertrådning: ThreadPool (20 trådar), ComputeWorker, mutex + condition variables
- IPC: FIFO, Unix socket, POSIX shared memory — alla tre mekanismer
- C++-klienten använder RAII (SocketGuard) och STL (unique_ptr, vector, map)

---

## BLOCK 3 — Live-demo (4–5 min)

> *Byt till terminalen. Ha två fönster: ett för server-output, ett för curl/klient.*

---

**[GÖR:]**

```bash
# Terminal 1 — starta systemet
make dev
```

> Vänta tills du ser "HTTP server started on port 8080" i loggarna.

```bash
# Terminal 2 — hämta en prognos
./bin/GridGuard-client forecast
```

**[SÄG medan det körs:]**

Nu skickar klienten en HTTP-request till servern. Servern kollar cachen — cache miss, så den skickar en WorkRequest via FIFO till Fetcher. Fetcher hämtar väderdata och spotpriser, skickar vidare till Parser via FIFO, Parser skickar ParseResult via Unix socket till ComputeWorker som beräknar energiplanen och lagrar den i shared memory. Sen väcker ComputeWorker HTTP-tråden via en condition variable.

**[VISA output:]**

> Peka på JSON-svaret och förklara: "Här ser vi rekommendationen — köp el 02:00–05:00 när priset är lågt, sälja överskott 12:00–14:00 under solpeak."

```bash
# Visa att watchdog-metrics exponeras
curl http://localhost:8080/metrics
```

> "Här ser vi processtatistik direkt från watchdog via shared memory — PID:ar, heartbeat-ålder, antal omstarter."

```bash
# Om tid finns — visa hot-reload
kill -HUP $(cat /tmp/gridguard.pid)
# "Vi kan ladda om konfigurationen utan omstart via SIGHUP"
```

---

**[OM DEMO KRASCHAR:]**

> Ha en terminalinspelning (asciinema eller liknande) som backup.
> Säg lugnt: "Vi visar inspelningen istället" och fortsätt utan att be om ursäkt.

---

## BLOCK 4 — Profileringsresultat (3 min)

> *Visa tabell eller diagram — ta fram PROFILING_REPORT.md eller rita siffror på tavlan*

---

**[SÄG:]**

Vi har profilerat systemet med `gprof`, benchmarks med `clock_gettime`, och minnesläckagetest med Valgrind.

**Flaskhalsen är compute-steget** — `Compute_GenerateEnergyPlan` tar 100% av CPU-tid i sin modul. Men det är ändå snabbt:

| | -O0 (före) | -O2 (efter) | Förbättring |
|---|---|---|---|
| avg latens | 3.22 µs | 2.10 µs | 1.53× snabbare |
| p99 latens | 29.51 µs | 8.38 µs | **3.52× snabbare** |
| genomströmning | 310k/sek | 477k/sek | +54% |

Den viktigaste förbättringen var att gå från `-O0` till `-O2`. Kompilatorn inlinar de inre looparna och eliminerar stack frame-overhead — p99 sjönk med 3.5×.

**[SÄG:]**

Perspektiv: systemet behöver beräkna en ny plan var 15:e minut. Med 477 000 planer per sekund har vi en marginal på **7 miljoner gånger**. Vi är inte compute-bound.

Shared memory-cachen klarar 2.4 miljoner concurrent lookups/sekund med 8 trådar. Valgrind hittade noll minnesläckor i produktionskoden.

---

## BLOCK 5 — Designval och lärdomar (3 min)

---

**[SÄG:]**

**Varför separata processer istället för bara trådar?**

Isolation. Om Fetcher kraschar vid ett nätverksfel eller mallformad JSON-respons drar det inte ner hela servern. Watchdog omstartar gruppen transparent. Det är ett medvetet val — vi betalar lite extra i IPC-overhead, men vi fick processavgränsning och en tydlig kontraktsgräns: structs som `WorkRequest`, `FetchResult`, `ParseResult` är den enda kommunikationen.

**Varför FIFO istället för anonym pipe?**

Watchdog kör `fork + execv` — anonyma pipes ärvs bara vid fork i samma process. Namngivna FIFOs kan öppnas av oberoende processer utan att dela file descriptors.

**Det svåraste problemet vi stötte på:**

Heap corruption. Vi lade till ett nytt fält i `HTTPClient`-structen men körde med stale `.o`-filer. Processen kraschade slumpmässigt för att struct-layouten i minnet inte stämde med headern. Lösning: `make clean && make all`. Lärdomen: utan `-MMD` dependency tracking i Makefilen måste man alltid göra clean-build efter header-ändringar.

**C++-klienten:**

Vi implementerade RAII via `SocketGuard` — destruktorn stänger socket automatiskt, även vid exception. `std::unique_ptr` för nätverksbuffertar, `std::vector<std::string>` för HTTP-headers. Inga manuella free/delete.

---

## BLOCK 6 — Avslut (1 min)

---

**[SÄG:]**

Sammanfattningsvis: GridGuard är ett fungerande systemnära program med multi-process IPC-pipeline, trådpool, POSIX shared memory med process-shared rwlocks, och en RAII-baserad C++-klient. Systemet profilerat, minnesläckagetestrat, och dokumenterat.

Vi är redo för frågor.

---

## Förväntade frågor — svar

**"Varför dödar ni ALLA processer vid en enskild krasj?"**
> FIFOs är enkelriktade och blockerar. Om Fetcher dör har Parser ingen write-end — `read()` returnerar EOF och Parser hänger ändå. En atomär omstart är mer förutsägbar än att hantera partiella tillstånd.

**"Vad händer om Watchdog kraschar?"**
> Då finns inget som startar om systemet — det är en känd begränsning. I produktion skulle man köra watchdog under systemd eller en init-process.

**"Hur vet HTTP-tråden när ComputeWorker är klar?"**
> CompletionRegistry — en mutex-skyddad array med condition variables. HTTP-tråden registrerar en WorkCompletion med userId, skickar WorkRequest via FIFO, och blockerar på `pthread_cond_timedwait` (30s timeout). ComputeWorker slår upp userId i registret och signalerar condition variable.

**"Varför är cache miss snabbare än cache hit?"**
> Cache miss avbryter `memcmp`-jämförelsen tidigt (early exit vid första tecken som skiljer). Cache hit kräver `memcpy` av hela data-payloaden (upp till 32KB) — det är memcpy som dominerar latensen, inte sökningen.

**"Hur hanterar ni race conditions i shared memory?"**
> `pthread_rwlock_t` lagrad *inuti* shared memory-segmentet med `PTHREAD_PROCESS_SHARED`-attribut. Flera readers parallellt, writer får exklusivt lås. Kernel hanterar synkroniseringen via futex.

---

## Tidplan

| Block | Tid | Kumulativt |
|-------|-----|-----------|
| Intro | 2 min | 2 min |
| Arkitektur | 3 min | 5 min |
| Live-demo | 5 min | 10 min |
| Profilering | 3 min | 13 min |
| Designval | 3 min | 16 min |
| Avslut | 1 min | 17 min |
| Buffer/frågor | 3 min | 20 min |

---

## Checklista innan presentation

- [ ] `make clean && make all` på presentationsdatorn
- [ ] `make dev` — verifiera att systemet startar rent
- [ ] Test-curl mot `/forecast` — verifiera svar
- [ ] Terminal-font stor nog att synas på projektor
- [ ] Backup-inspelning av demo tillgänglig
- [ ] Profilingstabellen redo att visa
