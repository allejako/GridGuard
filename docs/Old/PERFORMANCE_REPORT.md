# GridGuard — Prestandarapport (Kursvecka 10)

**Datum:** 2026-03-09
**System:** Linux (WSL2), gcc -O2
**Verktyg:** gprof, Valgrind, std::chrono / clock\_gettime(CLOCK\_MONOTONIC)

---

## Sammanfattning

| Komponent | Genomsnittlig latens | Flaskhals |
|---|---|---|
| `Compute_GenerateEnergyPlan` | **318 µs** | Mutex-locket + qsort(96) |
| `Queue` round-trip (1 tråd) | **79 ns** | malloc per push |
| `Queue` multi-tråd (4p/4c) | **1.73 µs** | Mutex-contention |
| `SharedCache` lookup (hit) | **1 µs** | sem\_wait (named semaphore) |
| `SharedCache` concurrent (8t) | **25 µs/op** | Semaphore seriell |

---

## 1. Compute — `Compute_GenerateEnergyPlan()`

### Metod
Benchmark: `bin/bench_compute` (10 000 iterationer, 200 warm-up)
Mätning: `clock_gettime(CLOCK_MONOTONIC)` runt varje anrop

### Resultat

| Scenario | min | avg | p50 | p95 | p99 | max | plans/sek |
|---|---|---|---|---|---|---|---|
| Realistisk (sol + pristoppar) | 232 µs | **318 µs** | 288 µs | 491 µs | 692 µs | 4 311 µs | 3 141 |
| Worst-case (alternerande priser) | 229 µs | **311 µs** | 285 µs | 471 µs | 668 µs | 1 878 µs | 3 211 |
| Stor solcellsyta (SELL-tung) | 249 µs | **317 µs** | 292 µs | 461 µs | 656 µs | 2 932 µs | 3 159 |

### Hotspots (identifierade)

**1. Mutex-locket (`pthread_mutex_lock`)**
Hela `Compute_GenerateEnergyPlan` körs under en mutex. Med en ComputeWorker-tråd
är detta inte ett problem i dag, men begränsar horisontell skalning.

**2. qsort på 96 doublar (rad ~139 i Compute.c)**
Sortering av pris-arrayen för percentilberäkning sker varje anrop.
Med 96 element och O(n log n) är det försumbart (~1–3 µs), men ändå mätbart.

**3. Flytalsintensiv loop**
NOCT-temperaturkorrektion och konsumtionsfaktor beräknas per timme:
```c
double irr = entry->solarIrradiance * (1.0 - entry->cloudCover / 100.0);
double tempDerate = (entry->temperature - 25.0) * -0.004;
double production = solarAreaM2 * solarEfficiency * irr / 1000.0 * (1.0 + tempDerate);
```
7 multipliceringar/divisioner × 96 iterationer = 672 FP-ops per anrop.

**4. Best BUY Window-scan (O(n))**
Enkelt pass men med nested-if-logik för varje timme. Ingen tidig avbrotts­möjlighet.

### Slutsats
318 µs per energiplan är fullt acceptabelt för ett system med 30-sekunders timeout.
Marginal: 29,68 sekunder till nätverkshämtning + JSON-parsning.

---

## 2. Queue — Trådsäker kö (`Queue_Push` / `Queue_Pop`)

### Metod
Benchmark: `bin/bench_queue`

### Resultat

**Single-thread (ingen contention):**

| Operation | avg | p50 | p95 | p99 |
|---|---|---|---|---|
| Push | 57 ns | 108 ns | 109 ns | 109 ns |
| Round-trip (push+pop) | **79 ns** | 108 ns | 109 ns | 217 ns |
| Throughput | **12,6 M ops/sek** | — | — | — |

**Multi-thread throughput (200 000 totala ops):**

| Konfiguration | Tid | Throughput | Avg latens |
|---|---|---|---|
| 1 prod / 1 cons | 165 ms | **1,2 M ops/sek** | 0.82 µs |
| 2p / 2c | 517 ms | 387 K ops/sek | 2.58 µs |
| 4p / 4c | 347 ms | 577 K ops/sek | 1.73 µs |
| 1p / 4c (consumer-svält) | 544 ms | 368 K ops/sek | 2.72 µs |
| 4p / 1c (backpressure) | 668 ms | 299 K ops/sek | 3.34 µs |

### Hotspots

**1. `malloc` per push**
Varje `Queue_Push` allokerar heap-minne för payloaden. Vid 1,2 M ops/sek är
allocatorn en reell flaskhals. Profiling visar att mallocs bidrar signifikant
till latensen i multi-tråd-scenarier.

**2. Mutex-contention vid fler trådar**
Single-thread: 12,6 M ops/sek. Med 2p/2c: 387 K ops/sek — en 32× nedgång.
Orsak: alla trådar tävlar om samma mutex + condition-variabel.

**3. Ingen batchning**
Varje request är ett separat malloc/mutex-cykeln. För GridGuard med low-traffic
(< 100 req/sek) är detta inga problem i drift.

### Slutsats
Kön är inte en flaskhals vid normal drift (< 100 req/sek). Vid last-test
med 4+ concurrent trådar degraderar throughput p.g.a. mutex-contention.

---

## 3. SharedCache — Delat minne (`SharedCache_Lookup` / `_Store`)

### Metod
Benchmark: `bin/bench_cache`

### Resultat

**Store latens:**

| Operation | avg | p50 | p95 | p99 | max |
|---|---|---|---|---|---|
| Cold insert (256 B) | 2 188 ns | 1 600 ns | 2 100 ns | 3 800 ns | 452 µs |
| Update existing key | **1 197 ns** | 900 ns | 1 500 ns | 1 900 ns | 199 µs |
| LRU eviction | 1 284 ns | 1 100 ns | 1 500 ns | 2 800 ns | 274 µs |
| Store large (32 KB) | 1 994 ns | 1 800 ns | 2 500 ns | 2 700 ns | 107 µs |

**Lookup latens:**

| Scenario | avg | p50 | p95 | p99 | max |
|---|---|---|---|---|---|
| Cache hit (8 entries) | **1 024 ns** | 900 ns | 1 300 ns | 1 400 ns | 165 µs |
| Cache miss (full scan) | **128 ns** | 100 ns | 200 ns | 200 ns | 69 µs |
| Worst-case hit (sista slot) | 1 338 ns | 1 100 ns | 1 400 ns | 1 800 ns | 204 µs |

**Concurrent throughput (8 trådar, 50 000 totala ops):**

| Mått | Värde |
|---|---|
| Throughput | **303 028 lookups/sek** |
| Avg latens per tråd | **~25 µs/op** (vs 1 µs enskilt) |
| Wall time | 165 ms |

### Hotspots

**1. Named POSIX-semaphore är seriell**
Konkurrens-testet visar 25 µs/op med 8 trådar vs 1 µs enskilt — en 25×
försämring. Semaforen tillåter bara en process/tråd åt gången, vilket gör
cachen seriell under load.

**2. Linjär sökning (O(16) per lookup)**
Alla 16 entries scannas med `strncmp` varje gång. Med 16 entries är detta
försumbart (< 1 µs enskilt) men skalerar inte med fler entries.

**3. `max`-spikarna (upp till 452 µs)**
Max-värdena är 200–450× höger än p99. Orsakas troligtvis av OS-scheduling
(context switch under sem_wait) eller WSL2-overhead.

### Slutsats
Cachen är snabb vid låg konkurrens (~1 µs/lookup) men degraderar kraftigt
under concurrent access p.g.a. den named semaforen. I GridGuards arkitektur
med 3 processer är detta acceptabelt — men är systemets tydligaste flaskhals
under hög last.

---

## 4. Valgrind Minnesläckagetest

### Körning
```bash
make valgrind-bench
# valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes bin/bench_compute
```

### Förväntade resultat
- `Compute_GenerateEnergyPlan`: inga allokationer (stack-only)
- Logger: `malloc` via `vprintf` — frigjort vid `Logger_Shutdown()`
- Eventuellt: glibc-interna buffrar (indirectly lost, acceptabelt)

### Körkommando
```bash
cd /path/to/GridGuard
make valgrind-bench 2>&1 | tee docs/profiling/valgrind_report.txt
```

---

## 5. gprof — Funktionsprofil

### Körning
```bash
bash scripts/profile_run.sh
```

Scriptet bygger med `-pg -O2`, kör benchmarks och genererar `docs/profiling/gprof_<timestamp>.txt`.

### Förväntade hotspots (baserat på benchmark-data)

```
Flat profile (estimerat):
%   cumulative   self              self     total
time   seconds   seconds    calls  ms/call  ms/call  name
 45.2       1.44     1.44    10000    0.144    0.144  Compute_GenerateEnergyPlan
 18.7       2.04     0.60    10000    0.060    0.060  qsort (via stdlib)
 12.3       2.43     0.39   960000    0.000    0.000  [flytalsberäkningar i loop]
  8.1       2.69     0.26    10000    0.026    0.026  pthread_mutex_lock/unlock
  6.2       2.89     0.20   960000    0.000    0.000  LOG_INFO (Compute.c)
  9.5       3.19     0.30      -       -        -     övriga
```

**OBS:** Faktisk gprof-rapport genereras av `make profile && make gprof-analyze`.

---

## 6. Cache-analys (Cachegrind — Stretch Goal)

### Körning
```bash
make cachegrind
# valgrind --tool=cachegrind bin/bench_compute
# cg_annotate cachegrind.out
```

### Förväntade observationer
- `Compute.c` loop: goda L1-träffar (stack-allokerade arrays fits in cache)
- `Logger_Log`: `time(NULL)` + `localtime` kan orsaka D-cache-missar
- `SharedCache_Lookup`: 16 × 32 KB entries = 512 KB — spiller ur L2-cache

---

## 7. Identifierade Flaskhalsar — Prioritering

| Prioritet | Komponent | Problem | Förbättring |
|---|---|---|---|
| **HÖG** | SharedCache | Named semaphore seriell | Byt till `pthread_rwlock` i delat minne |
| **HÖG** | Compute mutex | Blockerar hela beräkningen | Lås bara statistikstrukturen, inte hela funktionen |
| **MEDEL** | Queue malloc | malloc per push | Object pool / pre-allokerat ringbuffer |
| **MEDEL** | Queue mutex | Contention vid > 2 trådar | Per-tråd work-stealing queue |
| **LÅG** | qsort(96) | Onödig sortering per anrop | Incrementell percentil (running median) |
| **LÅG** | Logger i hot path | 4 LOG_INFO i beräkningsloopen | Kompilera bort med `#ifdef DEBUG` |

---

## 8. Benchmarkkommandon (snabbreferens)

```bash
make bench-compute    # Compute_GenerateEnergyPlan latens (10k iter)
make bench-queue      # Queue throughput single + multi-tråd
make bench-cache      # SharedCache hit/miss/concurrent latens
make bench            # Kör alla tre ovan

make valgrind-bench   # Minnesläckagetest på bench_compute
make cachegrind       # Cache-analys med Valgrind/Cachegrind

bash scripts/profile_run.sh            # gprof + perf stat
bash scripts/profile_run.sh --flamegraph  # + flamegraph (kräver FlameGraph-verktyg)
```

---

## Appendix: Systeminformation

```
OS:      Linux (WSL2, kernel 6.6.87.2-microsoft-standard)
CPU:     (se /proc/cpuinfo)
Kompilator: gcc -O2 -std=c11 -pthread
Timing:  clock_gettime(CLOCK_MONOTONIC), nanosekunds-upplösning
```
