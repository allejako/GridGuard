# Cache-refaktorering: Flytta cache till Fetch-nivå

*Skapad: 2026-02-16*

---

## Sammanfattning

Flytta cachning från ComputeStage (efter beräkning) till FetchStage (efter API-anrop) för att möjliggöra cache-delning mellan klienter med olika konfigurationer.

---

## Problemanalys

### Nuvarande implementation

```
Request → Fetch → Parse → Compute → CacheStage
                                        ↓
                                  Cache EnergyPlan
                                  (klient-specifik)
```

**Problem:** Cachad EnergyPlan innehåller beräkningar baserade på en specifik klients konfiguration (solpaneler, batteri, förbrukning). Andra klienter kan inte använda denna cache.

### Önskad implementation

```
Request → FetchStage → Parse → Compute → Svar
              ↓
        ┌─────┴─────┐
        ▼           ▼
   WeatherCache  PriceCache
   (per plats)   (per region)
```

**Fördel:** Väderdata och spotpriser är gemensamma för alla klienter i samma geografiska område.

---

## Designbeslut

### Cache-nycklar

| Cache | Nyckel | Exempel |
|-------|--------|---------|
| Weather | `"{lat}_{lon}"` | `"59.33_18.07"` |
| Price | `"{region}_{date}"` | `"SE3_2026-02-16"` |

### TTL (Time-To-Live)

| Cache | TTL | Motivering |
|-------|-----|------------|
| Weather | 15 min | API uppdateras varje timme, 15 min ger bra balans |
| Price | 15 min | Samma TTL som väderdata för konsistens |

### Datastrukturer

```c
// WeatherCache entry
typedef struct {
    char key[32];           // "59.33_18.07"
    char jsonData[32768];   // Rå JSON från API
    time_t createdAt;
    bool occupied;
} WeatherCacheEntry;

// PriceCache entry
typedef struct {
    char key[32];           // "SE3_2026-02-16"
    char jsonData[16384];   // Rå JSON från API
    time_t createdAt;
    bool occupied;
} PriceCacheEntry;
```

---

## Implementeringsplan

### Steg 1: Skapa nya cache-strukturer

**Nya filer:**
- `src/pipeline/components/WeatherCache.h`
- `src/pipeline/components/WeatherCache.c`
- `src/pipeline/components/PriceCache.h`
- `src/pipeline/components/PriceCache.c`

**Funktioner per cache:**
```c
int WeatherCache_Initiate(WeatherCache *cache, int ttlSeconds);
void WeatherCache_Shutdown(WeatherCache *cache);
int WeatherCache_Store(WeatherCache *cache, double lat, double lon, const char *json);
int WeatherCache_Lookup(WeatherCache *cache, double lat, double lon, char *jsonOut, size_t maxLen);
```

### Steg 2: Uppdatera PipelineOrchestrator

**Ändra i `PipelineOrchestrator.h`:**
```c
typedef struct Pipeline {
    // ... befintliga fält ...

    // Ersätt:
    // Cache cache;

    // Med:
    WeatherCache weatherCache;
    PriceCache priceCache;
} Pipeline;
```

**Ändra i `PipelineOrchestrator.c`:**
- Initiera `WeatherCache` och `PriceCache` i `Pipeline_Initiate()`
- Stäng av båda i `Pipeline_Shutdown()`

### Steg 3: Uppdatera FetchStage

**Ändra i `FetchStage.c`:**

```c
void *FetchStage_Work(void *arg) {
    // ...

    // FÖRE API-anrop - kolla cache
    char cachedWeatherJson[32768];
    bool weatherCached = (WeatherCache_Lookup(
        &pipeline->weatherCache, lat, lon,
        cachedWeatherJson, sizeof(cachedWeatherJson)) == 0);

    if (!weatherCached) {
        // Fetch från API
        Fetcher_Fetch(&pipeline->fetcher, weatherUrl, &weatherResp);

        // Spara i cache
        WeatherCache_Store(&pipeline->weatherCache, lat, lon, weatherResp.data);
        strncpy(result->weatherJson, weatherResp.data, ...);
    } else {
        // Använd cachad data
        strncpy(result->weatherJson, cachedWeatherJson, ...);
        LOG_INFO("Fetch: Weather cache HIT");
    }

    // Samma logik för priser...
}
```

### Steg 4: Ta bort gammal cache

**Ta bort:**
- `src/pipeline/components/Cache.h`
- `src/pipeline/components/Cache.c`
- `src/pipeline/stages/CacheStage.h`
- `src/pipeline/stages/CacheStage.c`

**Uppdatera:**
- Ta bort `cacheThread` från Pipeline
- Ta bort `computeQueue` (Compute skickar direkt till klient)
- Flytta klient-svar från CacheStage till ComputeStage

### Steg 5: Uppdatera ComputeStage

**Ändra i `ComputeStage.c`:**
- Ta bort cache-lookup (finns nu i FetchStage)
- Skicka svar direkt till klient (istället för att pusha till computeQueue)

```c
void *ComputeStage_Work(void *arg) {
    // ... beräkna plan ...

    // Skicka svar direkt till klient
    char response[4096];
    // ... formatera response ...
    send(parseData->clientFd, response, len, 0);

    // Ingen push till computeQueue
}
```

### Steg 6: Uppdatera Makefile

Ta bort:
- `CacheStage.o`
- `Cache.o`

Lägg till:
- `WeatherCache.o`
- `PriceCache.o`

### Steg 7: Uppdatera tester

Uppdatera `test_pipeline.c` för ny arkitektur.

---

## Ny arkitektur efter refaktorering

```
                         ┌─────────────────┐
                         │  WeatherCache   │
                         │  TTL: 15 min    │
                         │  Key: lat_lon   │
                         └────────┬────────┘
                                  │
┌────────┐   ┌────────────┐       │       ┌────────────┐   ┌──────────────┐
│ Client │──▶│ FetchStage │───────┼──────▶│ ParseStage │──▶│ ComputeStage │──▶ Svar
└────────┘   └────────────┘       │       └────────────┘   └──────────────┘
                                  │
                         ┌────────┴────────┐
                         │   PriceCache    │
                         │   TTL: 15 min   │
                         │  Key: region_   │
                         │       date      │
                         └─────────────────┘
```

**Pipeline-trådar:** 3 (istället för 4)
- FetchThread
- ParseThread
- ComputeThread

**Köer:** 3 (istället för 4)
- requestQueue
- fetchQueue
- parseQueue

---

## Filändringar sammanfattning

| Fil | Åtgärd |
|-----|--------|
| `pipeline/components/WeatherCache.h/c` | NY |
| `pipeline/components/PriceCache.h/c` | NY |
| `pipeline/components/Cache.h/c` | TA BORT |
| `pipeline/stages/CacheStage.h/c` | TA BORT |
| `pipeline/stages/FetchStage.c` | ÄNDRA - lägg till cache-logik |
| `pipeline/stages/ComputeStage.c` | ÄNDRA - skicka svar direkt |
| `pipeline/PipelineOrchestrator.h` | ÄNDRA - nya cache-typer |
| `pipeline/PipelineOrchestrator.c` | ÄNDRA - initiera/stäng nya caches |
| `Makefile` | ÄNDRA - uppdatera källfiler |
| `tests/test_pipeline.c` | ÄNDRA - anpassa till ny arkitektur |

---

## Risker och överväganden

1. **Koordinater vs stadsnamn:** Nuvarande implementation använder stadsnamn, men API:et använder lat/lon. Behöver konsekvent nyckelformat.

2. **JSON-storlek:** Väder-JSON kan vara stor (~30KB). Överväg att parsa och cacha structs istället för rå JSON.

3. **Thread-safety:** Båda cacharna måste vara trådsäkra (mutex).

4. **Minnesanvändning:** 64 entries × 32KB = ~2MB för väder-cache. Acceptabelt.

---

## Testplan

1. **Enhetstester:**
   - WeatherCache store/lookup/expiry
   - PriceCache store/lookup/expiry

2. **Integrationstester:**
   - Första request → cache MISS → API-anrop
   - Andra request (samma plats) → cache HIT → inget API-anrop
   - Request efter TTL → cache MISS → API-anrop igen

3. **Prestandatest:**
   - Mät responstid med/utan cache
   - Verifiera att API-anrop minskar vid upprepade requests

---

## Definition of Done

- [ ] WeatherCache implementerad och testad
- [ ] PriceCache implementerad och testad
- [ ] FetchStage använder caches
- [ ] Gamla Cache/CacheStage borttagna
- [ ] ComputeStage skickar svar direkt
- [ ] Pipeline har 3 trådar istället för 4
- [ ] Alla tester passerar
- [ ] Dokumentation uppdaterad
