# Ändringslogg - 2026-02-12

## Övergripande förändringar
Idag har jag fokuserat på att förbättra projektets arkitektur genom att eliminera global state och börjat arbetet med bättre separation of concerns

## Tydlig struktur

Compute har förenklats till att använda en enda tråd enligt bästa praxis för pipeline-arkitektur.

Nu följer systemet en tydlig struktur där varje pipeline-steg har exakt en tråd: Fetch-tråden hämtar data från API:er, Parse-tråden tolkar JSON, och Compute-tråden gör energiberäkningarna. Detta är mer effektivt och följer producer-consumer mönstret utan onödig overhead från thread-hantering.

EnergyData innehåller bara grundläggande utility-funktioner för att konvertera action enums till strängar och validera data.

## Eliminering av global state

Tidigare låg Pipeline som en global variabel i main.c, vilket gjorde det svårt att testa och gav dålig kodstruktur. Nu äger Server-structen alla komponenter istället - Pipeline, ThreadPool och TCPServer. Pipeline skickas sedan vidare till ThreadPool genom dependency injection, så worker threads kan använda den utan att någonting behöver vara globalt.

## Ny Server-modul för bättre struktur

En helt ny Server.h/c har skapats som kapslar in all initialiseringslogik och hantering av serverkomponenterna. Detta gör att main.c nu är minimal - bara 34 rader - och följer mönstret: skapa server, kör server, stäng ner server. All komplexitet är flyttad till Server-modulen där den hör hemma.

Server-structen äger alla tre huvudkomponenterna och ansvarar för deras livscykel. När servern initieras startas först Pipeline, sedan ThreadPool (som får en referens till Pipeline), och slutligen TCPServer. Vid nedstängning sker allt i omvänd ordning för att garantera ren uppsädning.

## Omorganisering av filer

SignalHandler flyttades från src/server till src/common eftersom den inte är server-specifik utan en generell utility för att hantera signaler som SIGINT och SIGTERM. Den passar bättre ihop med Logger och andra gemensamma verktyg.

ClientHandler.h/c och utils.h/c togs bort helt eftersom de bara innehöll tomma stubs utan någon kod. Det var meningslöst att behålla dem och kunde bara skapa förvirring om vad som faktiskt är implementerat.

## Resultat

Projektet har nu en mycket tydligare struktur med:
- Ingen global state
- Tydlig separation mellan server-specifik kod och gemensamma utilities
- Dependency injection istället för globala variabler
- Mock-implementationer som följer pipeline-arkitektur med en tråd per steg
- En minimal main-funktion som är lätt att förstå

Alla tester passerar fortfarande.

## Förbättringar för nästa commit

### Thread-safety
Fetcher, Parser och Compute saknar för närvarande intern mutex-skydd. Även om varje komponent har sin egen dedikerade tråd och det tekniskt inte är nödvändigt just nu, skulle det följa bästa praxis att lägga till pthread_mutex för att skydda intern state (isInitialized, konfigurationer, etc). Detta skulle göra komponenterna fullt thread-safe och förberedda för framtida ändringar där flera trådar kanske behöver dela samma instanser.

### Mappstruktur och kodorganisation

**Threads-mappen och kodstruktur**
ThreadPool.c (333 rader) och PipelineThreads.c (564 rader) har flera designproblem:

1. **Queue-implementation är inte återanvändbar**
   - Queue ligger inline i PipelineThreads.c istället för att vara en separat datastruktur
   - Borde extraheras till common/collections/Queue.h/c eller common/sync/Queue.h/c
   - Fast storlek (100) - LinkedList.h/c skulle ge dynamisk storlek och bättre minneshantering

2. **ThreadPool blandar concerns**
   - Client-hantering (ClientState, Client struct) borde inte ligga i ThreadPool
   - ThreadPool ska bara hantera worker threads, inte client protocol
   - Client-hantering borde flyttas till server/ClientHandler.h/c

3. **PipelineThreads.c gör för mycket**
   - 564 rader med Queue-implementation, tre worker-funktioner och orchestration
   - Borde delas upp i mindre, fokuserade moduler

4. **Saknar Scheduler abstraction**
   - Ingen centraliserad task scheduling-logik
   - Load balancing mellan workers är primitiv
   - En concurrency/Scheduler.h/c skulle kunna hantera:
     - Task distribution och prioritering
     - Work stealing mellan workers
     - Load balancing strategier

**Två möjliga vägar framåt:**

**Alternativ A: Behåll pipeline-mappen men organisera bättre**
```
src/
  pipeline/
    PipelineOrchestrator.h/c  # Koordinerar pipeline stages
    stages/
      FetchStage.h/c    # Fetch-worker logik
      ParseStage.h/c    # Parse-worker logik
      ComputeStage.h/c  # Compute-worker logik
    components/
      Fetcher.h/c       # Själva API-logiken
      Parser.h/c        # Själva parse-logiken
      Compute.h/c       # Själva beräkningslogiken

  concurrency/
    sync/Queue.h/c
    pool/ThreadPool.h/c
    Scheduler.h/c

  server/
    ClientHandler.h/c
```

**Alternativ B: Ta bort pipeline-mappen, domän-baserad struktur (rekommenderad)**
```
src/
  energy/
    Compute.h/c         # Energiberäkningar
  network/
    Fetcher.h/c         # API-anrop
  parsing/
    Parser.h/c          # JSON-parsing

  workflows/            # eller orchestration/
    Orchestrator.h/c    # Koordinerar workflow stages
    stages/
      FetchStage.h/c
      ParseStage.h/c
      ComputeStage.h/c

  concurrency/
    sync/Queue.h/c
    pool/ThreadPool.h/c
    Scheduler.h/c

  server/
    ClientHandler.h/c
```

**Alternativ B** ger bättre separation - domänlogiken (Compute, Fetcher, Parser) ligger i sina egna mappar, medan workflow-logiken (orchestration, stages) ligger i en egen mapp. Detta gör varje komponents ansvar tydligare.

Dessa förändringar skulle ge:
- Bättre separation of concerns
- Återanvändbara komponenter (Queue, Scheduler)
- Lättare att testa varje del isolerat
- Mer professionell och skalbar arkitektur
