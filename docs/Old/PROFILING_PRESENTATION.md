# Kursvecka 10 — Profilering och Prestandaanalys
### GridGuard (LEOP) · 2026-03-09
### Kursmål 6 & 10 — Profilering och identifiering av flaskhalsar

---

## Agenda

1. Systembakgrund och arkitektur
2. Mätmetodik
3. Compute — hotspots och latens
4. Queue — throughput och contention
5. SharedCache — semaphore-flaskhals
6. Valgrind — minnesläckagetest
7. Kritisk path-analys
8. Förslag på bättre paths
9. Sammanfattning

---

## Systembakgrund

**LEOP** — Local Energy Optimization Platform
Lokalt körbart system för prognostisering och optimering av solenergi.
Kombinerar väderdata med spotprisdata för optimala tider för elförbrukning och försäljning.

**Arkitekturkrav från spec (obligatoriska):**
- Multi-threaded design med tydlig pipeline: `fetch → parse → compute → cache`
- Minst en del körs i separat process
- Kommunikation via Unix domain sockets, pipes eller shared memory

**Implementerad pipeline:**
```
HTTP-klient
    │
    ▼
ThreadPool  ──────────────────────────────────────────────────────────┐
    │ write(pipe)                                                      │
    ▼                                                                  │
Fetcher-process                                                        │
    │ write(FIFO)                              WorkCompletion_Wait()   │
    ▼                                          (blockar upp till 30 s) │
Parser-process                                                         │
    │ write(Unix socket)                                               │
    ▼                                                                  │
ComputeWorker-tråd                                                     │
    │ Compute_GenerateEnergyPlan()                                     │
    │ JSON-serialisering                                               │
    │ WorkCompletion_Signal()                                          │
    └──────────────────────────────────────────────────────────────────┘
                                              HTTP-svar
```

---

## Mätmetodik

| Verktyg | Används till | Kommando |
|---|---|---|
| `clock_gettime(CLOCK_MONOTONIC)` | Latens per funktion, ns-upplösning | `make bench` |
| `gprof -pg` | Funktionsprofil, call graph | `make profile && make gprof-analyze` |
| `valgrind --leak-check=full` | Minnesläckor och heap-fel | `make valgrind-bench` |
| `valgrind --tool=cachegrind` | L1/L2 cache-analys | `make cachegrind` |

**Tre benchmark-binärer:**
- `bin/bench_compute` — 10 000 iterationer × 3 scenarier
- `bin/bench_queue` — single-thread + 5 multi-tråd-konfigurationer
- `bin/bench_cache` — store / lookup / concurrent 8 trådar

Alla: warm-up-fas → mätfas → percentilberäkning

---

## Compute — `Compute_GenerateEnergyPlan()`

### Mätresultat (10 000 iterationer, 96-timmars forecast)

| Scenario | avg | p50 | p95 | p99 | plans/sek |
|---|---|---|---|---|---|
| Realistisk (sol + pristoppar) | **318 µs** | 288 µs | 491 µs | 692 µs | 3 141 |
| Worst-case (alternerande priser) | 311 µs | 285 µs | 471 µs | 668 µs | 3 211 |
| Stor solcellsyta (SELL-tung) | 317 µs | 292 µs | 461 µs | 656 µs | 3 159 |

### Identifierade hotspots

**1. Mutex håller hela funktionen (~318 µs)**
```c
pthread_mutex_lock(&compute->mutex);   // låser in
// ← 96 iterations FP-beräkningar
// ← qsort(96)
// ← BUY-window scan
pthread_mutex_unlock(&compute->mutex); // låser ut
```
Begränsar skalning om fler ComputeWorker-trådar läggs till.

**2. qsort(96 doublar) varje anrop**
Sortering för percentilberäkning (p30/p70) sker varje anrop.
O(n log n), ~1–3 µs — kan ersättas med Welfords running median.

**3. 672 FP-operationer per anrop**
```c
double irr        = solarIrradiance * (1.0 - cloudCover / 100.0);
double tempDerate = (temperature - 25.0) * -0.004;
double production = area * efficiency * irr / 1000.0 * (1.0 + tempDerate);
```
7 mult/div × 96 timmar = 672 FP-ops. Stretch goal: SIMD-optimering.

**Slutsats:** 318 µs är acceptabelt — 29,68 s marginal till 30 s timeout.

---

## Queue — `Queue_Push` / `Queue_Pop`

### Single-thread (ingen contention)

| Operation | avg | p50 | p95 | throughput |
|---|---|---|---|---|
| Push | 57 ns | 108 ns | 109 ns | — |
| Round-trip (push+pop) | **79 ns** | 108 ns | 109 ns | **12,6 M ops/sek** |

### Multi-thread throughput (200 000 ops totalt)

| Konfiguration | Throughput | Avg latens | vs 1p/1c |
|---|---|---|---|
| 1p / 1c | 1 215 K ops/sek | 0.82 µs | — |
| 2p / 2c | 387 K ops/sek | 2.58 µs | **−68%** |
| 4p / 4c | 577 K ops/sek | 1.73 µs | −53% |
| 1p / 4c | 368 K ops/sek | 2.72 µs | −70% |
| 4p / 1c | 299 K ops/sek | 3.34 µs | −75% |

### Identifierade hotspots

**1. `malloc` per push** — varje request allokerar heap-minne för payloaden

**2. Mutex-contention** — 2p/2c tappar 68% throughput vs 1p/1c
Alla trådar tävlar om samma mutex + condition-variabel.

**Slutsats:** Inte en flaskhals vid GridGuards normala last (< 100 req/sek).
Spec:ens stretch goal: *"Dynamisk trådpool baserad på systemlast"* — relevant vid hög last.

---

## SharedCache — Delat minne

### Store-latens

| Operation | avg | p50 | p95 |
|---|---|---|---|
| Cold insert (256 B) | 2 188 ns | 1 600 ns | 2 100 ns |
| Update existing key | **1 197 ns** | 900 ns | 1 500 ns |
| LRU eviction | 1 284 ns | 1 100 ns | 1 500 ns |
| Store large (32 KB) | 1 994 ns | 1 800 ns | 2 500 ns |

### Lookup-latens

| Scenario | avg | p50 | p95 |
|---|---|---|---|
| Cache hit (8 entries) | **1 024 ns** | 900 ns | 1 300 ns |
| Cache miss (full scan) | **128 ns** | 100 ns | 200 ns |
| Worst-case hit (sista slot av 16) | 1 338 ns | 1 100 ns | 1 400 ns |

### Concurrent (8 trådar, 50 000 ops)

| Enskilt | 8 trådar | Försämring |
|---|---|---|
| 1 µs/op | **25 µs/op** | **25×** |

### Identifierade hotspots

**1. Named POSIX-semaphore är helt seriell — systemets största flaskhals**
```
tråd 0 ──► sem_wait ──► lookup ──► sem_post ─┐
tråd 1 ───────────────────────────────────────► sem_wait ──► ...
tråd 2 ──────────────────────────────────────────────────────► ...
```
Spec:ens stretch goal: *"Läs-skriv-lås för bättre parallellism på cache"*

**2. Linjär sökning O(16)** — scannar alla entries med `strncmp` varje lookup

**Slutsats:** 1 µs enskilt → 25 µs under 8 trådar. Acceptabelt med 3 processer i dag.

---

## Valgrind — Minnesläckagetest

```bash
make valgrind-bench
# valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes
```

### Resultat

```
HEAP SUMMARY:
    in use at exit: 0 bytes in 0 blocks
  total heap usage: 31 689 allocs, 31 689 frees, 485 517 bytes allocated

All heap blocks were freed -- no leaks are possible

ERROR SUMMARY: 0 errors from 0 contexts
```

- **Noll minnesläckor** — alla 31 689 allokeringar frigjorda korrekt
- `Compute_GenerateEnergyPlan` använder **enbart stack** — noll heap i hot-path
- Valgrind-overhead: ~15× (4,9 ms vs 318 µs) — bekräftar stack-only compute

---

## Kritisk path-analys

### Nuvarande latensbudget (per request, cache-miss)

```
Steg                           Estimerad tid
──────────────────────────────────────────────
Nätverkshämtning (Fetcher)     1 000–2 000 ms   ← dominerande
JSON-parsning (Parser)              ~100 ms
IPC-overhead (pipe+FIFO+sock)        ~10 ms
SharedCache-lookup                    ~1 µs
Compute_GenerateEnergyPlan          ~318 µs
JSON-serialisering                   ~50 µs
──────────────────────────────────────────────
Totalt                         ~1 100–2 100 ms   (av 30 s budget)
```

### Det verkliga problemet

Nätverkshämtningen (1–2 s) dominerar totalt — compute (318 µs) är **3 000× snabbare**.

Men nuvarande design: **varje HTTP-request triggar alltid Fetcher → Parser**,
även när data redan finns i SharedCache (TTL: 15 min).
Spec:en kräver: *"Lagra prognoser lokalt med cache och TTL"* — men short-circuit saknas.

---

## Förslag på bättre paths

Alla förslag är direkt hämtade från spec:ens stretch goals och kursvecka 11.

---

### Förslag 1 — Cache short-circuit (störst impact)

**Problem:** Fetch + Parse körs varje gång, oavsett om datan finns i cachen (TTL: 15 min).

**Nuvarande path (alltid):**
```
HTTP → pipe → Fetcher (~1-2 s) → FIFO → Parser (~100 ms) → socket → Compute → svar
```

**Föreslagen path:**
```
HTTP → SharedCache lookup (1 µs) ──HIT──► Compute (318 µs) → svar
                                 ──MISS──► Fetcher → Parser → Compute → cache → svar
```

**Var:** `ClientHandler.c:HandleForecast()` — lägg SharedCache-lookup före `GridGuard_SubmitRequest()`

**Förväntad förbättring:** 1–2 s → ~320 µs vid träff (**×3 000 snabbare**)
**Spec-referens:** *"Lagra prognoser lokalt med cache och TTL"* + *"Distribuerad cache med invalidering"*

---

### Förslag 2 — Läs-skriv-lås för SharedCache

**Problem:** Named POSIX-semaphore tillåter bara en tråd/process åt gången.
25× försämring under 8 trådar.

**Nuvarande:**
```c
sem_wait(cache->sem);   // exklusivt — läsare blockerar läsare
// ... lookup ...
sem_post(cache->sem);
```

**Föreslagen:**
```c
pthread_rwlock_rdlock(&region->rwlock);   // många läsare parallellt
// ... lookup ...
pthread_rwlock_unlock(&region->rwlock);

pthread_rwlock_wrlock(&region->rwlock);   // exklusivt vid skrivning
// ... store ...
pthread_rwlock_unlock(&region->rwlock);
```

**Förväntad förbättring:** 25 µs → ~1 µs vid concurrent lookups
**Spec-referens:** *"Läs-skriv-lås för bättre parallellism på cache"* (kursvecka 3, stretch goal)

---

### Förslag 3 — `poll()` timeout i ComputeWorker

**Problem:** `ComputeWorker_Run()` blockerar på `read()` utan timeout.
Om Parser hänger → ComputeWorker hänger för alltid.

**Nuvarande:**
```c
ssize_t n = read(fd, &result, sizeof(result));  // blockerar indefinitely
```

**Föreslagen:**
```c
struct pollfd pfd = { .fd = fd, .events = POLLIN };
int ready = poll(&pfd, 1, 5000);  // 5 s timeout
if (ready <= 0) { /* timeout/error — logga och återanslut */ }
ssize_t n = read(fd, &result, sizeof(result));
```

**Förväntad förbättring:** Robusthet mot hängande Parser-process
**Spec-referens:** *"Multiplexad I/O med select() eller poll()"* + *"Felhantering för API-fel, timeout"*

---

### Förslag 4 — Zero-copy I/O för Fetcher → Parser

**Problem:** Data kopieras tre gånger: Fetcher-buffer → FIFO → Parser-buffer → Unix socket.

**Föreslagen (splice — zero-copy kernel-to-kernel):**
```c
splice(fifo_fd, NULL, sock_fd, NULL, data_size, SPLICE_F_MOVE);
```

**Förväntad förbättring:** Reducerar CPU-belastning vid stora datamängder (32 KB weather-JSON)
**Spec-referens:** *"Zero-copy I/O för stora dataöverföringar"* (stretch goal)

---

### Förslag 5 — SIMD för beräkningsloopen

**Problem:** 672 FP-ops per anrop körs seriellt, en i taget.

**Föreslagen (AVX2, 4 doubles parallellt):**
```c
#include <immintrin.h>
for (int i = 0; i < n; i += 4) {
    __m256d prices = _mm256_loadu_pd(&spotPrice[i]);
    __m256d fees   = _mm256_set1_pd(gridFee + energyTax + vat);
    __m256d total  = _mm256_add_pd(prices, fees);
    _mm256_storeu_pd(&costs[i], total);
}
```

**Förväntad förbättring:** 4× throughput på priskostnadsberäkning
**Spec-referens:** *"SIMD-optimeringar för numeriska beräkningar"* (kursvecka 11 stretch goal)

---

## Sammanfattning av flaskhalsar

| Prioritet | Komponent | Problem | Förbättring | Impact |
|---|---|---|---|---|
| 🔴 **HÖG** | Request-path | Fetcher+Parser körs alltid | Cache short-circuit | **×3 000** |
| 🔴 **HÖG** | `SharedCache` | Semaphore seriell (25× försämring) | `pthread_rwlock` | ×25 |
| 🟡 **MEDEL** | `ComputeWorker` | Ingen timeout på socket-read | `poll()` med timeout | Robusthet |
| 🟡 **MEDEL** | `Queue` | malloc per push, mutex-contention | Object pool | ×2–3 |
| 🟢 **LÅG** | IPC pipe→FIFO | Data kopieras 3 ggr | `splice()` zero-copy | CPU-last |
| 🟢 **LÅG** | `Compute` loop | Seriell FP-beräkning | SIMD (AVX2) | ×4 |

---

## Slutsats

### Kursmål uppfyllda (vecka 10)

| Krav | Status |
|---|---|
| Profileringsrapport med gprof/perf | `bash scripts/profile_run.sh` |
| Minnesläckagetest med Valgrind | `make valgrind-bench` → **0 läckor** |
| Dokumenterade hotspots i koden | Compute mutex, Queue malloc, Cache semaphore |
| Benchmarks för kritiska operationer | 3 binärer, 10 000 iterationer vardera |
| Cache-analys (stretch) | `make cachegrind` |

### Nästa steg — Kursvecka 11

Implementera förbättringar med **före/efter-mätningar** (krav vecka 11):

1. **Cache short-circuit** — en if-sats i `HandleForecast()` → mätbar med bench
2. **`pthread_rwlock` i SharedCache** — ersätter named semaphore
3. **`poll()` timeout i ComputeWorker** — robusthet, inte latens

### Latensbudget efter optimering (estimerat)

```
Cache-träff (efter short-circuit):  ~320 µs   (idag: ~1 200 ms)
Cache-miss (nätverkshämtning):      ~1 200 ms  (oförändrat — nätverksbegränsat)
```
