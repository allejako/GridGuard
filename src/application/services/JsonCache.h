#ifndef _JSON_CACHE_H_
#define _JSON_CACHE_H_

#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <stddef.h>

#define JSON_CACHE_MAX_ENTRIES      64
#define JSON_CACHE_DEFAULT_TTL_SEC  900   // 15 minutes
#define JSON_CACHE_KEY_MAX          64
#define JSON_CACHE_DATA_MAX         32768 // Sized for largest response (weather JSON)

// A single cached JSON entry
typedef struct
{
    char key[JSON_CACHE_KEY_MAX];
    char jsonData[JSON_CACHE_DATA_MAX];
    time_t createdAt;
    bool occupied;
} JsonCacheEntry;

// Generic JSON cache — stores raw API responses keyed by an arbitrary string
typedef struct
{
    JsonCacheEntry entries[JSON_CACHE_MAX_ENTRIES];
    int ttlSeconds;
    pthread_mutex_t mutex;
    bool isInitialized;
} JsonCache;

int  JsonCache_Initiate(JsonCache *cache, int ttlSeconds);
void JsonCache_Shutdown(JsonCache *cache);

// Store JSON under key. Returns 0 on success.
int  JsonCache_Store(JsonCache *cache, const char *key, const char *json);

// Look up JSON by key. Returns 0 on hit (writes to jsonOut), -1 on miss or expiry.
int  JsonCache_Lookup(JsonCache *cache, const char *key, char *jsonOut, size_t maxLen);

#endif // _JSON_CACHE_H_
