#include "cache.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

typedef struct {
    CacheEntry entries[MAX_CACHE_ENTRIES];
    int count;
    pthread_mutex_t lock;
    unsigned long hits;
    unsigned long misses;
} Cache;

static Cache cache = {
    .count = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .hits = 0,
    .misses = 0
};

static int findEntry(const char *key);
static int findFreeSlot(void);
static bool isExpired(const CacheEntry *entry);

int cacheInit(void){
    pthread_mutex_lock(&cache.lock);

    for (int i = 0; i < MAX_CACHE_ENTRIES; i++){
        cache.entries[i] .isValid = false;
        memset(cache.entries[i] .key, 0, MAX_KEY_LENGTH);
        memset(cache.entries[i] .data, 0, MAX_DATA_LENGTH);
        cache.entries[i] .timestamp = 0;
        cache.entries[i] .ttlSeconds = DEFAULT_TTL_SECONDS;
    }
    cache.count = 0;
    cache.hits = 0;
    cache.misses = 0;

    pthread_mutex_unlock(&cache.lock);

    printf("[CACHE] Init OK - %d slots\n", MAX_CACHE_ENTRIES);
    return 0;
}


int cacheSet(const char *key, const char *data, int ttlSeconds) {
    if (!key || !data) {
        fprintf(stderr, "[CACHE] Error: null pointer\n");
        return -1;
    }
    
    if (strlen(key) >= MAX_KEY_LENGTH || strlen(data) >= MAX_DATA_LENGTH) {
        fprintf(stderr, "[CACHE] Error: data too large\n");
        return -1;
    }
    
    pthread_mutex_lock(&cache.lock);
    
    int index = findEntry(key);
    
    if (index == -1) {
        index = findFreeSlot();
        if (index == -1) {
            pthread_mutex_unlock(&cache.lock);
            fprintf(stderr, "[CACHE] Error: cache full\n");
            return -1;
        }
        cache.count++;
    }
    
    strncpy(cache.entries[index].key, key, MAX_KEY_LENGTH - 1);
    strncpy(cache.entries[index].data, data, MAX_DATA_LENGTH - 1);
    cache.entries[index].timestamp = time(NULL);
    cache.entries[index].ttlSeconds = ttlSeconds;
    cache.entries[index].isValid = true;
    
    pthread_mutex_unlock(&cache.lock);
    
    printf("[CACHE] set: %s (ttl=%ds)\n", key, ttlSeconds);
    return 0;
}

const char* cacheGet(const char *key) {
    if (!key) return NULL;
    
    pthread_mutex_lock(&cache.lock);
    
    int index = findEntry(key);
    
    if (index == -1) {
        cache.misses++;
        pthread_mutex_unlock(&cache.lock);
        printf("[CACHE] miss: %s\n", key);
        return NULL;
    }
    
    CacheEntry *entry = &cache.entries[index];
    
    if (isExpired(entry)) {
        entry->isValid = false;
        cache.count--;
        cache.misses++;
        pthread_mutex_unlock(&cache.lock);
        printf("[CACHE] expired: %s\n", key);
        return NULL;
    }
    
    cache.hits++;
    pthread_mutex_unlock(&cache.lock);
    printf("[CACHE] hit: %s\n", key);
    
    return entry->data;
}

bool cacheIsValid(const char *key) {
    if (!key) return false;
    
    pthread_mutex_lock(&cache.lock);
    
    int index = findEntry(key);
    if (index == -1) {
        pthread_mutex_unlock(&cache.lock);
        return false;
    }
    
    bool valid = !isExpired(&cache.entries[index]);
    pthread_mutex_unlock(&cache.lock);
    
    return valid;
}

int cacheInvalidate(const char *key) {
    if (!key) return -1;
    
    pthread_mutex_lock(&cache.lock);
    
    int index = findEntry(key);
    if (index == -1) {
        pthread_mutex_unlock(&cache.lock);
        return -1;
    }
    
    cache.entries[index].isValid = false;
    cache.count--;
    
    pthread_mutex_unlock(&cache.lock);
    
    printf("[CACHE] invalidated: %s\n", key);
    return 0;
}

void cacheClear(void) {
    pthread_mutex_lock(&cache.lock);
    
    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        cache.entries[i].isValid = false;
    }
    cache.count = 0;
    
    pthread_mutex_unlock(&cache.lock);
    printf("[CACHE] cleared\n");
}

void cachePrintStats(void) {
    pthread_mutex_lock(&cache.lock);
    
    unsigned long total = cache.hits + cache.misses;
    double hitRate = total > 0 ? (double)cache.hits / total * 100.0 : 0.0;
    
    printf("\n--- Cache Stats ---\n");
    printf("Entries: %d/%d\n", cache.count, MAX_CACHE_ENTRIES);
    printf("Hits: %lu, Misses: %lu\n", cache.hits, cache.misses);
    printf("Hit rate: %.1f%%\n", hitRate);
    printf("-------------------\n\n");
    
    pthread_mutex_unlock(&cache.lock);
}

void cacheDestroy(void) {
    cacheClear();
    pthread_mutex_destroy(&cache.lock);
    printf("[CACHE] destroyed\n");
}

// helpers

static int findEntry(const char *key) {
    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        if (cache.entries[i].isValid && strcmp(cache.entries[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

static int findFreeSlot(void) {
    // try to find empty slot first
    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        if (!cache.entries[i].isValid) {
            return i;
        }
    }
    
    // cache full, evict oldest
    int oldestIdx = 0;
    time_t oldestTime = cache.entries[0].timestamp;
    
    for (int i = 1; i < MAX_CACHE_ENTRIES; i++) {
        if (cache.entries[i].timestamp < oldestTime) {
            oldestTime = cache.entries[i].timestamp;
            oldestIdx = i;
        }
    }
    
    printf("[CACHE] evicting: %s\n", cache.entries[oldestIdx].key);
    cache.entries[oldestIdx].isValid = false;
    cache.count--;
    
    return oldestIdx;
}

static bool isExpired(const CacheEntry *entry) {
    time_t now = time(NULL);
    time_t age = now - entry->timestamp;
    return (age >= entry->ttlSeconds);
}