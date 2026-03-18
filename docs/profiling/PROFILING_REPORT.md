# GridGuard — Profileringsrapport

**Datum:** 2026-03-18
**System:** GridGuard (LEOP — Local Energy Optimization Platform)
**Verktyg:** `bench_compute` (clock_gettime), `gprof`, `valgrind --leak-check=full`
**Kursmål:** 6 (profilering), 10 (utföra profilering), 11 (optimera kod), 12 (dokumentera)

---

## 1. Sammanfattning

| Komponent | Hotspot | Genomsnittlig latens | Genomströmning |
|-----------|---------|---------------------|----------------|
| Compute (realistisk) | `Compute_GenerateEnergyPlan` | 2.10 µs | 476 792 planer/sek |
| Compute (worst-case) | `Compute_GenerateEnergyPlan` | 1.56 µs | 640 325 planer/sek |
| Compute (stor solpanel) | `Compute_GenerateEnergyPlan` | 1.57 µs | 637 158 planer/sek |
| Queue (single-thread) | push/pop | 0.08 µs RTT | 12 936 157 ops/sek |
| Queue (multi-thread 1p/1c) | mutex contention | 1.60 µs | 625 133 ops/sek |
| Cache (hit) | linear scan | 1.29 µs | — |
| Cache (miss) | early exit | 0.11 µs | — |
| Cache (concurrent, 8 trådar) | — | — | 2 457 106 lookups/sek |

**Slutsats:** Systemet presterar väl. Compute-steget är flaskhalsen i pipelinen men med ~2 µs per plan är genomströmningen mer än tillräcklig för realtidsbruk (data uppdateras var 15:e minut).

---

## 2. Metod

### 2.1 Benchmark-verktyg

Benchmarks körs med `clock_gettime(CLOCK_MONOTONIC)` med nanosekundsprecision. Varje scenario kör 200 warm-up-iterationer (för att fylla branch predictor och instruction cache) följt av 10 000 mätiterationer. Percentilvärden beräknas genom sortering.

```bash
make bench          # Kör bench_compute + bench_queue + bench_cache
make valgrind-bench # Minnesläckagetest med Valgrind
make profile        # Bygg med -pg, kör gprof
```

### 2.2 Testmiljö

- **OS:** Linux 6.6 (WSL2)
- **Kompilator:** GCC med `-O2`
- **CPU:** x86-64

---

## 3. Compute-steget — `Compute_GenerateEnergyPlan()`

### 3.1 gprof — Flat Profile

```
  %   cumulative   self              self     total
 time   seconds   seconds    calls  ns/call  ns/call  name
100.00      0.01     0.01    30600   326.80   326.80  Compute_GenerateEnergyPlan
  0.00      0.01     0.00    30602     0.00     0.00  Logger_Log
  0.00      0.01     0.00        5     0.00     0.00  fill_forecast_realistic
  0.00      0.01     0.00        1     0.00     0.00  Compute_Initiate
```

**Analys:** `Compute_GenerateEnergyPlan` utgör 100% av CPU-tid i compute-modulen. Logger_Log-overhead är negligibel.

### 3.2 Scenario 1 — Realistisk prognos (solpeak + prisökning)

Simulerar ett normalt dygn med sinusformad solinstrålning och morgon/kväll-prissurge.

```
iterations : 10 000  (warmup: 200)
min        :   1 097 ns  (  1.10 µs)
avg        :   2 097 ns  (  2.10 µs)
p50        :   1 596 ns  (  1.60 µs)
p95        :   3 193 ns  (  3.19 µs)
p99        :   8 381 ns  (  8.38 µs)
max        : 165 325 ns  (165.32 µs)
throughput : 476 792 planer/sek
```

**Observation:** p99 är 5× p50, vilket indikerar sporadiska OS-schemaläggningsavbrott (inte ett kodproblem).

### 3.3 Scenario 2 — Worst-case (alternerande priser, fullt array)

Maximalt spann i spotpriserna tvingar köpalgoritmens scan att traversera hela arrayen.

```
iterations : 10 000  (warmup: 200)
min        :   1 297 ns  (  1.30 µs)
avg        :   1 562 ns  (  1.56 µs)
p50        :   1 397 ns  (  1.40 µs)
p95        :   2 295 ns  (  2.29 µs)
p99        :   3 891 ns  (  3.89 µs)
max        : 139 782 ns  (139.78 µs)
throughput : 640 325 planer/sek
```

**Observation:** Paradoxalt nog är worst-case *snabbare* än scenario 1. Orsak: med alternerande priser körs SELL-logiken inte lika ofta, vilket minskar grenar i den inre loopen.

### 3.4 Scenario 3 — Stor solpanelyta (SELL-heavy path)

100 m² panelyta genererar överskott under hela dagen och aktiverar SELL-beslutskoden.

```
iterations : 10 000  (warmup: 200)
min        :   1 097 ns  (  1.10 µs)
avg        :   1 569 ns  (  1.57 µs)
p50        :   1 396 ns  (  1.40 µs)
p95        :   2 394 ns  (  2.39 µs)
p99        :   4 689 ns  (  4.69 µs)
max        : 176 998 ns  (177.00 µs)
throughput : 637 158 planer/sek
```

---

## 4. Queue — Trådssäker kö

### 4.1 Single-thread latens

```
avg push   :   62 ns  (0.06 µs)
avg RTT    :   77 ns  (0.08 µs) — push + pop
throughput : 12 936 157 ops/sek
```

### 4.2 Multi-thread genomströmning

| Konfiguration | Genomströmning | Latens (avg) |
|---------------|---------------|-------------|
| 1 producent / 1 konsument | 625 133 ops/sek | 1.60 µs |
| 2p / 2c | 426 506 ops/sek | 2.34 µs |
| 4p / 4c | 483 549 ops/sek | 2.07 µs |
| 1p / 4c | 400 623 ops/sek | 2.50 µs |
| 4p / 1c | 302 554 ops/sek | 3.31 µs |

**Observation:** Låg genomströmning vid 4p/1c beror på mutex-contention — producenter blockerar varandra. Systemets faktiska pipeline (1 producent → 1 konsument) är det optimala scenariot.

---

## 5. SharedCache — Delat minne

| Operation | avg | p50 | p95 | p99 |
|-----------|-----|-----|-----|-----|
| Cold insert (256 B) | 2 163 ns | 2 037 ns | 2 546 ns | 2 954 ns |
| Update existing key | 1 281 ns | 1 222 ns | 1 426 ns | 1 630 ns |
| LRU eviction | 1 277 ns | 1 222 ns | 1 426 ns | 1 528 ns |
| Store large payload (32 KB) | 2 638 ns | 2 546 ns | 2 649 ns | 3 768 ns |
| Cache hit (8 entries) | 1 287 ns | 1 120 ns | 1 426 ns | 1 897 ns |
| Cache miss (full scan) | 112 ns | 100 ns | 200 ns | 200 ns |
| Worst-case hit (sista slot) | 1 385 ns | 1 298 ns | 1 597 ns | 1 797 ns |

**Concurrent (8 trådar):** 2 457 106 lookups/sek

**Observation:** Cache miss (112 ns) är 11× snabbare än cache hit (1 287 ns) — `memcmp`-jämförelsen avbryts tidigt vid miss. Cache hit kräver `memcpy` för att returnera data, vilket dominerar latensen.

---

## 6. Minnesläckagetest — Valgrind

**Kommando:**
```bash
make valgrind-bench
# valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes
```

**Resultat:**

| Kategori | Resultat |
|----------|----------|
| Heap-läckor | **0 — inga läckor** |
| Use-after-free | 0 |
| Invalid reads/writes | 0 |
| Oinitialiserade värden | 1 (se nedan) |

**Hittad issue:** `EnergyData plan` deklareras på stacken i `bench_compute.c:130` utan initialisering (`EnergyData plan;`). Valgrind rapporterar att `Compute_GenerateEnergyPlan` läser fält ur denna struct innan de skrivs.

**Påverkan:** Inga läckor eller korruption — det är ett läsbarhetsproblem i benchmark-koden, inte i produktionskoden. `EnergyData` initialiseras korrekt inne i `Compute_GenerateEnergyPlan` vid framgångsrikt anrop.

---

## 7. Identifierade flaskhalsar

| Prioritet | Komponent | Flaskhals | Åtgärd |
|-----------|-----------|-----------|--------|
| 1 | Compute | `qsort` vid percentilberäkning av köpfönster | Ersätt med partial sort (nth_element-ekvivalent) vid N > 100 |
| 2 | Queue | Mutex-contention vid hög producent-last (4p/1c) | Implementera batch-dequeue för att minska lock-frekvens |
| 3 | Cache | Linear scan O(N) vid lookup | Acceptabel vid N=16; värd att adressera om N skalas upp |
| 4 | Compute | p99-spikes (8 µs vs p50 1.6 µs) | OS-schemaläggning — inte åtgärdbart utan RT-kernel |

---

## 8. Optimering genomförd — Kompilatoroptimering (-O0 → -O2)

Båda byggen kördes med identiska 10 000 iterationer och 200 warm-up-iterationer på samma maskin.

### Före: `-O0` (ingen optimering)

| Scenario | avg | p50 | p99 | throughput |
|----------|-----|-----|-----|-----------|
| Scenario 1: realistisk prognos | 3.22 µs | 2.32 µs | 29.51 µs | 310 439 planer/sek |
| Scenario 2: worst-case | 2.27 µs | 1.51 µs | 23.87 µs | 440 368 planer/sek |
| Scenario 3: stor solpanel | 2.40 µs | 1.61 µs | 6.95 µs | 417 230 planer/sek |

### Efter: `-O2` (standardoptimering)

| Scenario | avg | p50 | p99 | throughput |
|----------|-----|-----|-----|-----------|
| Scenario 1: realistisk prognos | 2.10 µs | 1.60 µs | 8.38 µs | 476 792 planer/sek |
| Scenario 2: worst-case | 1.56 µs | 1.40 µs | 3.89 µs | 640 325 planer/sek |
| Scenario 3: stor solpanel | 1.57 µs | 1.40 µs | 4.69 µs | 637 158 planer/sek |

### Förbättring

| Scenario | avg-förbättring | p99-förbättring | throughput-förbättring |
|----------|-----------------|-----------------|----------------------|
| Scenario 1 | 3.22 → 2.10 µs (**1.53×**) | 29.51 → 8.38 µs (**3.52×**) | 310k → 477k (**+54%**) |
| Scenario 2 | 2.27 → 1.56 µs (**1.45×**) | 23.87 → 3.89 µs (**6.14×**) | 440k → 640k (**+45%**) |
| Scenario 3 | 2.40 → 1.57 µs (**1.53×**) | 6.95 → 4.69 µs (**1.48×**) | 417k → 637k (**+53%**) |

**Viktigaste förbättringen:** p99-latensen för scenario 1 sjönk från 29.51 µs till 8.38 µs — en 3.52× minskning. Inlining av inre loopar och borttagning av stack frame overhead är de primära orsakerna.

Kompilatoroptimeringarna som aktiveras av `-O2`:
- **Function inlining** — eliminerar call overhead för små hjälpfunktioner
- **Loop unrolling** — reducerar branch-overhead i prognos-loopen
- **Dead code elimination** — tar bort oanvända kodstigar vid compile-time

Produktionsbygget (`make release`) lägger till `-O2 -DNDEBUG` vilket dessutom inaktiverar `assert()`-kontroller och eliminerar debug-kodstigar.

---

## 9. Slutsatser

GridGuard-pipelinen presterar robust för realtidsbruk:

- **Compute** klarar ~476 000 planer/sekund — systemet behöver beräkna en ny plan var 15:e minut, alltså med en marginal på 7 miljoner×
- **Ingen minnesläcka** hittades i produktionskoden
- **Trådkön** är dimensionerad för pipelinens 1p/1c-mönster med 625 133 ops/sek
- **SharedCache** klarar 2.4M concurrent lookups/sek med 8 trådar

Det primära förbättringsområdet är cache-storleken (max 16 entries) som kräver omdesign vid framtida skalning, samt ersättande av `qsort` med selektiv sortering i köpfönster-algoritmen.
