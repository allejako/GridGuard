#ifndef _CACHE_H_
#define _CACHE_H_

#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include "Energy.h"

#define CACHE_MAX_ENTRIES 64
#define CACHE_DEFAULT_TTL_SECONDS 900  // 15 minutes

// A single cached energy plan
typedef struct
{
    char key[80];          // Cache key: "location/region"
    EnergyData plan;       // The cached energy plan
    time_t createdAt;      // When this entry was cached
    int ttlSeconds;        // Time-to-live in seconds
    bool occupied;         // Whether this slot is in use
} CacheEntry;

// Cache component
typedef struct
{
    CacheEntry entries[CACHE_MAX_ENTRIES];
    int count;
    int ttlSeconds;
    pthread_mutex_t mutex;
    bool isInitialized;
} Cache;

int Cache_Initiate(Cache *cache, int ttlSeconds);
void Cache_Shutdown(Cache *cache);

// Store an energy plan in cache. Returns 0 on success.
int Cache_Store(Cache *cache, const char *location, const char *region, const EnergyData *plan);

// Look up a cached energy plan. Returns 0 if found and not expired, -1 otherwise.
int Cache_Lookup(Cache *cache, const char *location, const char *region, EnergyData *planOut);

// Build the cache key from location and region
void Cache_BuildKey(const char *location, const char *region, char *keyOut, size_t keySize);

#endif // _CACHE_H_
