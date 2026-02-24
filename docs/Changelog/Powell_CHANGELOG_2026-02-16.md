# Ändringslogg - 2026-02-16

## Cache-steg i pipeline

### Nytt pipeline-steg: Cache
Pipeline:n har utökats från tre steg till fyra: `fetch → parse → compute → cache`, enligt projektspecifikationen.

### Nya filer
- `src/pipeline/components/Cache.h/c` — Cache-komponent med TTL-stöd (Time-To-Live)
- `src/pipeline/stages/CacheStage.h/c` — Cache-trådens worker-funktion

### Ändringar
- `PipelineOrchestrator.h/c` — Lagt till `cacheThread`, `computeQueue` och `Cache` i Pipeline-structen. Initierar och startar cache-tråden.
- `ComputeStage.c` — Kollar cache innan beräkning (cache hit = skippar Compute). Pushar resultat till `computeQueue` istället för att skicka direkt till klient.

### Hur cachen fungerar
- Energiplaner lagras med nyckeln `location/region` (t.ex. `"SE3/SE3"`)
- TTL på 15 minuter — efter det räknas datan som utgången
- Cache-lookup sker i Compute-steget innan beräkning
- Cache-lagring sker i Cache-steget efter att resultatet tagits emot
- Mutex-skyddad för trådsäkerhet
- Max 64 entries, äldsta ersätts om cachen är full

### Testat
- Första anropet: Cache MISS → hämtar från API → sparar i cache
- Andra anropet: Cache HIT → skippar API och beräkning → returnerar cachad data
