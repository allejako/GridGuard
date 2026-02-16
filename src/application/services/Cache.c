#include "Cache.h"
#include "Logger.h"
#include <string.h>
#include <stdio.h>

void Cache_BuildKey(const char *location, const char *region, char *keyOut, size_t keySize)
{
    snprintf(keyOut, keySize, "%s/%s", location, region);
}

int Cache_Initiate(Cache *cache, int ttlSeconds)
{
    if (!cache)
        return -1;

    memset(cache->entries, 0, sizeof(cache->entries));
    cache->count = 0;
    cache->ttlSeconds = ttlSeconds > 0 ? ttlSeconds : CACHE_DEFAULT_TTL_SECONDS;
    cache->isInitialized = true;
    pthread_mutex_init(&cache->mutex, NULL);

    LOG_INFO("Cache: Initiated with TTL=%d seconds, max entries=%d",
             cache->ttlSeconds, CACHE_MAX_ENTRIES);
    return 0;
}

void Cache_Shutdown(Cache *cache)
{
    if (!cache || !cache->isInitialized)
        return;

    pthread_mutex_lock(&cache->mutex);
    memset(cache->entries, 0, sizeof(cache->entries));
    cache->count = 0;
    cache->isInitialized = false;
    pthread_mutex_unlock(&cache->mutex);

    pthread_mutex_destroy(&cache->mutex);
    LOG_INFO("Cache: Shutdown complete");
}

int Cache_Store(Cache *cache, const char *location, const char *region, const EnergyData *plan)
{
    if (!cache || !cache->isInitialized || !plan)
        return -1;

    char key[80];
    Cache_BuildKey(location, region, key, sizeof(key));

    pthread_mutex_lock(&cache->mutex);

    // Check if key already exists, update it
    for (int i = 0; i < CACHE_MAX_ENTRIES; i++)
    {
        if (cache->entries[i].occupied && strcmp(cache->entries[i].key, key) == 0)
        {
            cache->entries[i].plan = *plan;
            cache->entries[i].createdAt = time(NULL);
            cache->entries[i].ttlSeconds = cache->ttlSeconds;
            pthread_mutex_unlock(&cache->mutex);
            LOG_INFO("Cache: Updated entry for key '%s'", key);
            return 0;
        }
    }

    // Find an empty slot or the oldest entry
    int targetSlot = -1;
    time_t oldestTime = time(NULL);

    for (int i = 0; i < CACHE_MAX_ENTRIES; i++)
    {
        if (!cache->entries[i].occupied)
        {
            targetSlot = i;
            break;
        }
        if (cache->entries[i].createdAt < oldestTime)
        {
            oldestTime = cache->entries[i].createdAt;
            targetSlot = i;
        }
    }

    if (targetSlot < 0)
        targetSlot = 0;

    if (!cache->entries[targetSlot].occupied)
        cache->count++;

    strncpy(cache->entries[targetSlot].key, key, sizeof(cache->entries[targetSlot].key) - 1);
    cache->entries[targetSlot].key[sizeof(cache->entries[targetSlot].key) - 1] = '\0';
    cache->entries[targetSlot].plan = *plan;
    cache->entries[targetSlot].createdAt = time(NULL);
    cache->entries[targetSlot].ttlSeconds = cache->ttlSeconds;
    cache->entries[targetSlot].occupied = true;

    pthread_mutex_unlock(&cache->mutex);
    LOG_INFO("Cache: Stored entry for key '%s' (slot %d, total: %d)", key, targetSlot, cache->count);
    return 0;
}

int Cache_Lookup(Cache *cache, const char *location, const char *region, EnergyData *planOut)
{
    if (!cache || !cache->isInitialized || !planOut)
        return -1;

    char key[80];
    Cache_BuildKey(location, region, key, sizeof(key));

    pthread_mutex_lock(&cache->mutex);

    for (int i = 0; i < CACHE_MAX_ENTRIES; i++)
    {
        if (!cache->entries[i].occupied)
            continue;

        if (strcmp(cache->entries[i].key, key) != 0)
            continue;

        // Check TTL
        time_t now = time(NULL);
        double age = difftime(now, cache->entries[i].createdAt);
        if (age > cache->entries[i].ttlSeconds)
        {
            // Expired, invalidate
            cache->entries[i].occupied = false;
            cache->count--;
            pthread_mutex_unlock(&cache->mutex);
            LOG_INFO("Cache: Entry for '%s' expired (age: %.0fs, TTL: %ds)",
                     key, age, cache->entries[i].ttlSeconds);
            return -1;
        }

        // Cache hit
        *planOut = cache->entries[i].plan;
        pthread_mutex_unlock(&cache->mutex);
        LOG_INFO("Cache: HIT for '%s' (age: %.0fs)", key, age);
        return 0;
    }

    pthread_mutex_unlock(&cache->mutex);
    LOG_DEBUG("Cache: MISS for '%s'", key);
    return -1;
}
