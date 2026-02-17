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

## Concurrency-arkitektur: ThreadPool, WorkerPool, Scheduler och Queue

### Arkitekturöversikt

GridGuards concurrency-system bygger på fyra samverkande komponenter som tillsammans hanterar parallell bearbetning och lastbalansering:

```
ThreadPool (specialiserad)
    └─> WorkerPool (generisk)
            ├─> Scheduler (väljer worker)
            └─> Worker[] (array av workers)

Queue (trådsäker datadelning mellan komponenter)
```

### WorkerPool - Den generiska grunden

**Plats:** `src/concurrency/threads/WorkerPool.h/c`

WorkerPool är det generiska fundamentet för alla worker-baserade operationer i GridGuard:

- **Ansvar:** Hanterar livscykeln för en pool av worker-trådar
- **Generisk design:** Tar emot en `WorkerThreadFunc` som definierar vad varje worker ska göra
- **Innehåller Scheduler:** Äger och använder en Scheduler för arbetsfördelning
- **Kontext-baserad:** Varje Worker har en `void *context` som kan vara vilken data som helst
- **Tvåstegs-initiering:**
  1. `WorkerPool_Initiate()` - Skapar workers och scheduler (startar INTE trådar)
  2. `WorkerPool_Start()` - Startar worker-trådar (context måste vara satt först)

**Varför tvåstegs-initiering?**
Detta mönster tillåter anroparen (t.ex. ThreadPool) att sätta worker-specifik context innan trådarna startar, vilket undviker race conditions.

### Scheduler - Intelligent arbetsfördelning

**Plats:** `src/concurrency/sync/Scheduler.h/c`

Scheduler avgör VILKEN worker som ska få nästa arbetsuppgift:

- **Tre policies:**
  - `LEAST_CONNECTIONS` - Väljer worker med minst antal aktiva klienter/tasks (standard)
  - `ROUND_ROBIN` - Roterar jämnt mellan workers (1→2→3→1)
  - `RANDOM` - Slumpmässigt val bland tillgängliga workers

- **Load-baserat val:** Tar emot `WorkerLoad[]` med info om varje workers nuvarande last och kapacitet
- **Thread-safe:** Använder mutex för att skydda scheduling-logiken

**Exempel från ThreadPool_AddClient (ThreadPool.c:209-223):**
```c
// Bygg load-info för alla workers
WorkerLoad workerLoads[numWorkers];
for (int i = 0; i < numWorkers; i++) {
    workerLoads[i].workerId = i;
    workerLoads[i].currentLoad = ctx->clientCount;
    workerLoads[i].capacity = MAX_CLIENTS_PER_THREAD;
}

// Låt scheduler välja bästa worker
int target = Scheduler_SelectWorker(&pool.scheduler, workerLoads, numWorkers);
```

### ThreadPool - Specialiserad för nätverks-I/O

**Plats:** `src/concurrency/threads/ThreadPool.h/c`

ThreadPool är en SPECIALISERING av WorkerPool för att hantera klient-anslutningar:

- **Bygger på WorkerPool:** Innehåller en `WorkerPool pool` som medlem (komposition)
- **Klient-kontext:** Varje worker får en `ThreadWorkerContext` med array av klienter
- **select()-baserad I/O:** Worker-funktionen använder `select()` för att multiplexera klienter
- **Scheduler-integration:** Använder WorkerPools inbyggda Scheduler (LEAST_CONNECTIONS) för att fördela nya klienter

**Initialiseringsflöde (ThreadPool.c:141-194):**
```
1. Allokera ThreadWorkerContext[] för alla workers
2. WorkerPool_Initiate() - Skapa WorkerPool (trådar ej startade)
3. Sätt context för varje Worker (worker->context = &contexts[i])
4. WorkerPool_Start() - Starta worker-trådar (nu är context säker att läsa)
```

**Worker-funktion (ThreadWorker_Work i ThreadPool.c:17-109):**
```
while (worker->isRunning) {
    1. Vänta på klienter (pthread_cond_wait om inga klienter)
    2. Bygg fd_set för select() från alla aktiva klienter
    3. select() - Vänta på aktivitet med timeout
    4. Hantera läsbara sockets (recv + Client_HandleState)
    5. Ta bort disconnected klienter
}
```

### Queue - Trådsäker pipeline-kommunikation

**Plats:** `src/concurrency/sync/Queue.h/c`

Queue används för att skicka data mellan olika komponenter/steg i systemet:

- **Thread-safe:** Mutex + condition variables (`notEmpty`, `notFull`)
- **Blocking operations:**
  - `Queue_Push()` - Blockerar om kön är full
  - `Queue_Pop()` - Blockerar om kön är tom
- **Generisk data:** `QueueItem` innehåller `void *data` + typ (`DataType`)
- **Typer:** `DATA_TYPE_REQUEST`, `DATA_TYPE_API_RESPONSE`, `DATA_TYPE_PARSED_DATA`, `DATA_TYPE_ENERGY_PLAN`

**Användning i pipeline (exempel från tidigare arkitektur):**
```
FetchStage --[requestQueue]--> ParseStage --[parseQueue]--> ComputeStage --[computeQueue]--> CacheStage
```

Varje steg:
1. Plockar data från sin input-queue (`Queue_Pop`)
2. Bearbetar datan
3. Pushar resultat till nästa queue (`Queue_Push`)

### Hur komponenterna samverkar

**Scenario: Ny klient ansluter till servern**

1. **Server accepterar klient:**
   - `accept()` returnerar ny `clientFd`
   - Anropar `ThreadPool_AddClient(threadPool, clientFd)`

2. **ThreadPool frågar Scheduler:**
   - Bygger `WorkerLoad[]` med info om alla workers nuvarande last
   - Anropar `Scheduler_SelectWorker()` för att få bästa worker
   - Scheduler returnerar worker-ID (t.ex. worker 2 har minst klienter)

3. **Klient läggs till vald worker:**
   - `ThreadWorkerContext_AddClient()` lägger klienten i ledig slot
   - Signalerar `pthread_cond_signal(&worker->cond)` för att väcka worker-tråd

4. **Worker-tråd hanterar klient:**
   - `select()` upptäcker aktivitet på klientens socket
   - `recv()` läser data från klienten
   - `Client_HandleState()` bearbetar förfrågan (kan använda Queues för pipeline-kommunikation)

### Designprinciper

**Separation of Concerns:**
- WorkerPool: Generisk worker-lifecycle
- Scheduler: Arbetsfördelningslogik
- ThreadPool: Domän-specifik implementation (nätverks-I/O)
- Queue: Trådsäker kommunikation mellan komponenter

**Återanvändbarhet:**
WorkerPool är generisk och kan återanvändas för andra typer av workers (inte bara nätverks-I/O).

**Testbarhet:**
Scheduler är fristående och kan testas isolerat med mock WorkerLoad-data.

**Thread-safety:**
Alla komponenter använder mutexes och condition variables för att garantera korrekt synkronisering.
