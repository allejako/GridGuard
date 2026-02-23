# Cache Refactoring Summary

*Completed: 2026-02-23*

---

## What Changed

The caching layer was moved from the end of the pipeline (after computation) to the beginning (before API calls). The two type-specific cache implementations were then consolidated into a single generic cache.

### Before

```
FetchWorker → ParseWorker → ComputeWorker → CacheWorker → Client
requestQueue  fetchQueue    parseQueue      computeQueue
                                                ↑
                                       Cache stores EnergyData
                                       (client-specific plan)
```

4 worker threads, 4 queues. The `CacheWorker` stored a fully computed `EnergyData` plan and sent the response to the client. The cache lookup happened in `ComputeWorker` — after fetching and parsing, but before computing. Because the cached value was a computed energy plan tied to a specific solar/battery/consumption config, different clients could not share it.

### After

```
FetchWorker → ParseWorker → ComputeWorker → Client
requestQueue  fetchQueue    parseQueue
    ↑
    JsonCache weatherCache  (key: "lat_lon")
    JsonCache priceCache    (key: "region_YYYY-MM-DD")
```

3 worker threads, 3 queues. `FetchWorker` checks the caches before making HTTP calls. Raw JSON from the weather and price APIs is cached instead of computed plans. `ComputeWorker` sends the response directly to the client.

---

## Where the Cache Lives

The caches are **server-side**, inside the `GridGuard` struct (`src/application/core/GridGuard.h`). They are initialized when the server starts and shared across all incoming client requests via the `FetchWorker` thread. The client has no caching logic.

---

## New Files

| File | Description |
|------|-------------|
| `src/application/services/JsonCache.h/.c` | Generic JSON string cache — used for both weather and price data |

### Cache Parameters

Both cache instances share the same `JsonCache` type:

| Instance | Key format | Max entries | TTL |
|----------|-----------|-------------|-----|
| `weatherCache` | `"59.33_18.07"` (lat_lon) | 64 | 15 min |
| `priceCache` | `"SE3_2026-02-23"` (region_date) | 64 | 15 min |

`JsonCache` stores up to 32 KB of JSON per entry (sized for the largest response). The caller is responsible for building the key string — `JsonCache` itself has no knowledge of weather vs. price data.

---

## Deleted Files

| File | Reason |
|------|--------|
| `src/application/services/Cache.h/.c` | Stored `EnergyData` (client-specific, not shareable) |
| `src/application/workers/CacheWorker.h/.c` | Pipeline stage no longer needed |
| `src/application/services/WeatherCache.h/.c` | Replaced by generic `JsonCache` |
| `src/application/services/PriceCache.h/.c` | Replaced by generic `JsonCache` |

---

## Why This Is Better

| | Old cache | New cache |
|---|-----------|-----------|
| **What is cached** | Computed energy plan | Raw API JSON |
| **Cache key** | `"location/region"` | `"lat_lon"` / `"region_date"` |
| **Shareable between clients** | No — plan is per-config | Yes — raw data is universal |
| **Pipeline stages** | 4 | 3 |
| **API calls saved** | Only for identical configs | For all clients in same area |
| **Cache implementations** | 1 (type-specific) | 1 generic (`JsonCache`) |

---

## Modified Files

| File | Change |
    |------|--------|
| `src/application/core/GridGuard.h` | Replaced `Cache`/`cacheThread`/`computeQueue` with two `JsonCache` fields |
| `src/application/core/GridGuard.c` | Updated init/shutdown; 3-thread log message |
| `src/application/workers/FetchWorker.h` | `weatherJson` buffer increased from 8 192 → 32 768 bytes |
| `src/application/workers/FetchWorker.c` | Builds cache keys inline; calls `JsonCache_Lookup`/`JsonCache_Store` for both weather and price |
| `src/application/workers/ComputeWorker.c` | Removed cache lookup and queue push; sends response directly to client |
