# Kursvecka 10 — Profilering och Prestandaanalys
### GridGuard · 2026-03-09

---

## Agenda

1. Systembakgrund
2. Mätmetodik
3. Compute — hotspots och latens
4. Queue — throughput och contention
5. SharedCache — semaphore-flaskhals
6. Valgrind — minnesläckagetest
7. Sammanfattning av flaskhalsar
8. Slutsats

---

## Systembakgrund

GridGuard är ett multi-process IPC-system för energioptimering.

```
HTTP-klient
    │
    ▼
ThreadPool (HTTP-workers)
    │  pipe
    ▼
Fetcher-process  ──FIFO──►  Parser-process  ──Unix socket──►  ComputeWorker-tråd
                                                                      │
                                                              Compute_GenerateEnergyPlan()
                                                                      │
                                                              WorkCompletion_Signal()
                                                                      │
                                                              ◄── HTTP-svar
```

**Kritisk path:** HTTP-request → pipe → Fetcher → FIFO → Parser → socket → Compute → JSON → svar
**Timeout:** 30 sekunder

---

## Mätmetodik

| Verktyg | Används till |
|---|---|
| `clock_gettime(CLOCK_MONOTONIC)` | Latens per funktion (ns-upplösning) |
| `gprof -pg` | Funktionsprofil, call graph |
| `valgrind --leak-check=full` | Minnesläckor och heap-fel |
| `valgrind --tool=cachegrind` | L1/L2 cache-analys |

**Benchmarks:**
- `bin/bench_compute` — 10 000 iterationer, 3 scenarier
- `bin/bench_queue` — single-thread + 5 multi-tråd-konfigurationer
- `bin/bench_cache` — store/lookup/concurrent med 8 trådar

Alla benchmarks: warm-up-fas → mätfas → sortering → percentiler

---

## Compute — `Compute_GenerateEnergyPlan()`

### Mätresultat (10 000 iterationer, 96 timmar forecast)

| Scenario | avg | p50 | p95 | p99 | plans/sek |
|---|---|---|---|---|---|
| Realistisk (sol + pristoppar) | **318 µs** | 288 µs | 491 µs | 692 µs | 3 141 |
| Worst-case (alternerande priser) | 311 µs | 285 µs | 471 µs | 668 µs | 3 211 |
| Stor solcellsyta (SELL-tung) | 317 µs | 292 µs | 461 µs | 656 µs | 3 159 |

### Identifierade hotspots

**1. Mutex håller hela funktionen**
```c
pthread_mutex_lock(&compute->mutex);
// ← ALL logik körs här inne: ~318 µs
pthread_mutex_unlock(&compute->mutex);
```
Begränsar skalning om fler ComputeWorker-trådar läggs till.

**2. qsort(96 doublar) varje anrop**
Sortering för percentilberäkning sker vid varje anrop.
96 element × O(n log n) ≈ 1–3 µs per anrop.

**3. 672 FP-operationer per anrop**
```c
double irr        = solarIrradiance * (1.0 - cloudCover / 100.0);
double tempDerate = (temperature - 25.0) * -0.004;
double production = area * efficiency * irr / 1000.0 * (1.0 + tempDerate);
```
7 mult/div × 96 timmar = 672 floating-point-ops per plan.

**Slutsats:** 318 µs är fullt acceptabelt — marginal till 30 s timeout är 29,68 s.

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
| 4p / 4c | 577 K ops/sek | 1.73 µs | **−53%** |
| 1p / 4c | 368 K ops/sek | 2.72 µs | −70% |
| 4p / 1c | 299 K ops/sek | 3.34 µs | −75% |

### Identifierade hotspots

**1. `malloc` per push** — varje request allokerar heap-minne
**2. Mutex-contention** — 2p/2c tappar 68% throughput vs 1p/1c
**3. Ingen batchning** — ett malloc/mutex-cykel per request

**Slutsats:** Inte en flaskhals vid GridGuards normala last (< 100 req/sek).

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

**1. Named POSIX-semaphore är helt seriell**
```
tråd 0 ──► sem_wait ──► lookup ──► sem_post ─┐
tråd 1 ──────────────────────────────────────► sem_wait ──► ...
tråd 2 ──────────────────────────────────────────────────────► sem_wait ──► ...
```
Alla 8 trådar köar bakom samma semafor → 25 µs avg vs 1 µs enskilt.

**2. Linjär sökning O(16)** — scannar alla entries med `strncmp` varje lookup

**3. Max-spikar upp till 452 µs** — OS context-switch under `sem_wait`

**Slutsats:** Systemets tydligaste flaskhals under hög last. Acceptabelt med 3 processer, men skalerar inte.

---

## Valgrind — Minnesläckagetest

```
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes bin/bench_compute_valgrind
```

### Resultat

```
HEAP SUMMARY:
    in use at exit: 0 bytes in 0 blocks
  total heap usage: 31 689 allocs, 31 689 frees, 485 517 bytes allocated

All heap blocks were freed -- no leaks are possible

ERROR SUMMARY: 0 errors from 0 contexts
```

### Slutsatser

- **Noll minnesläckor** — alla 31 689 allokeringar frigjorda korrekt
- `Compute_GenerateEnergyPlan` använder **enbart stack** — noll heap-allokationer i hot-path
- Logger-buffrar frigjorda korrekt vid `Logger_Shutdown()`
- Valgrind-overhead: ~15× (4,9 ms vs 318 µs normalt) — bekräftar stack-only compute

---

## Sammanfattning av flaskhalsar

| Prioritet | Komponent | Problem | Förbättring |
|---|---|---|---|
| 🔴 **HÖG** | `SharedCache` | Named semaphore → 25× försämring vid concurrency | Byt till `pthread_rwlock` i delat minne |
| 🔴 **HÖG** | `Compute` mutex | Hela beräkningen under ett lås | Lås bara statistikuppdateringen |
| 🟡 **MEDEL** | `Queue` malloc | Heap-allokering per request | Object pool / pre-allokerat ringbuffer |
| 🟡 **MEDEL** | `Queue` mutex | −68% throughput vid 2p/2c | Per-tråd work-stealing queue |
| 🟢 **LÅG** | qsort(96) | Onödig sortering varje anrop | Running median (Welford) |
| 🟢 **LÅG** | Logger i hot path | 4× `LOG_INFO` i beräkningsloop | `#ifdef DEBUG` guard |

---

## Slutsats

### Vad mätningarna visar

- **Compute-algoritmen** (318 µs) är snabb nog — 30 s timeout ger 29,68 s marginal
- **Queue** fungerar utmärkt vid normal last men degraderar vid hög concurrency
- **SharedCache** är systemets enda verkliga flaskhals — den named semaforen serialiserar all cache-åtkomst

### Systemet i helhet

Kritiska path-latens (estimerad):

```
Nätverkshämtning (Fetcher)    ~1–2 s
JSON-parsning (Parser)        ~100 ms
IPC-overhead (pipe/FIFO/sock) ~10 ms
Compute_GenerateEnergyPlan    ~318 µs
JSON-serialisering            ~50 µs
                              ─────────
Totalt                        ~1,2 s  (av 30 s budget)
```

### Kursmål uppfyllda

- Profilering med gprof (`make profile && make gprof-analyze`)
- Minnesanalys med Valgrind (`make valgrind-bench`) → 0 läckor
- Benchmarks med `clock_gettime` för tre kritiska komponenter
- Dokumenterade hotspots med prioritering och åtgärdsförslag
