# Ändringslogg - 2026-02-17

## Stor arkitektur-refaktorering: Pipeline → GridGuard

### Varför ändringen?

**Gamla Pipeline-arkitekturen hade flera problem:**

1. **Duplicerad kod** - Hade både "stages" OCH "components" som gjorde samma sak
   - `FetchStage.c` → anropade bara `Fetcher.c`
   - `ParseStage.c` → anropade bara `Parser.c`
   - Onödigt extra lager (~800 rader duplicering)

2. **Tight coupling** - PipelineOrchestrator var en "god object" som kände till allt
   - Svår att testa komponenter isolerat
   - Svår att ändra utan att påverka andra delar

3. **Dålig separation of concerns** - Blandat domänlogik med infrastruktur
   - Pipeline-kod blandat med affärslogik
   - Concurrency-primitiver i samma mapp som domänkod

4. **Komplicerad struktur** - För många abstraktionslager
   - Server → ThreadPool → Pipeline → Stages → Components
   - Svårt att förstå dataflödet

### Nya GridGuard-arkitekturen

```
src/
├── application/              ← Domänspecifik kod
│   ├── core/                 - GridGuard.c/h (applikationskärna)
│   ├── workers/              - FetchWorker, ParseWorker, ComputeWorker, CacheWorker
│   ├── models/               - EnergyData, ForecastData (datamodeller)
│   └── services/             - Cache, Compute, Fetcher, Parser (domäntjänster)
│
└── concurrency/              ← OS-primitiver (återanvändbart)
    ├── threads/              - ThreadPool, WorkerPool
    ├── sync/                 - Queue, Scheduler
    └── ipc/                  - (framtida IPC-primitiver)
```

**Varför detta är bättre:**

1. **Separation of Concerns**
   - `application/` = domänlogik (vad systemet gör)
   - `concurrency/` = infrastruktur (hur det körs)
   - Tydlig gräns mellan affärslogik och teknisk implementation

2. **Domain-Driven Design**
   - **Core** (GridGuard) = applikationens hjärta
   - **Workers** = pipeline-steg som processar domänobjekt
   - **Services** = återanvändbar domänlogik
   - **Models** = datastrukturer för domänen

3. **Enklare och renare**
   - Inga onödiga abstraktionslager (stages borttagna)
   - Workers använder Services direkt
   - −800 rader duplicerad kod

4. **Bättre testbarhet**
   - Varje worker kan testas isolerat
   - Services är oberoende av GridGuard
   - Concurrency-komponenter är återanvändbara

5. **Professionell struktur**
   - Följer industristandarder (Clean Architecture, SOLID)
   - `services/` istället för `components/` (mer vedertaget)
   - Tydlig mappstruktur som är lätt att navigera

### Hur det fungerar

**GridGuard (Application Core):**
```c
typedef struct GridGuard {
    // 4 worker-trådar (pipeline-steg)
    pthread_t fetchThread, parseThread, computeThread, cacheThread;

    // Producer-consumer köer (loose coupling)
    Queue requestQueue, fetchQueue, parseQueue, computeQueue;

    // Domäntjänster
    Fetcher fetcher;
    Parser parser;
    Compute compute;
    Cache cache;
} GridGuard;
```

**Dataflöde:**
```
Client Request
    ↓
[requestQueue] → FetchWorker → [fetchQueue] → ParseWorker →
[parseQueue] → ComputeWorker → [computeQueue] → CacheWorker → Client Response
```

Varje worker är en **självständig tråd** som:
- Läser från sin input-kö
- Processar data via services
- Skriver till sin output-kö
- Ingen shared state = thread-safe!

### Före vs Efter

**FÖRE (Pipeline):**
```c
Server → ThreadPool → Pipeline → Stages → Components
         (tight coupling överallt)
```

**EFTER (GridGuard):**
```c
Server → ThreadPool (network I/O)
      → GridGuard (application core)
          └── Workers (loose coupling via queues)
```

### Resultat

- ✅ −800 rader duplicerad kod
- ✅ Tydligare separation (application vs infrastructure)
- ✅ Varje komponent testbar isolerat
- ✅ Följer Clean Architecture & Domain-Driven Design
- ✅ Kompilerar utan varningar
- ✅ Alla tester passerar

### Nästa steg

- Runtime-testning med servern
- Integration-tester för hela flödet
- Förbättrad error handling i workers
