#include "JsonCache.h"
#include "Logger.h"
#include <string.h>
#include <stdio.h>

int JsonCache_Initiate(JsonCache *cache, int ttlSeconds)
{
    if (!cache)
        return -1;

    memset(cache->entries, 0, sizeof(cache->entries));
    cache->ttlSeconds = ttlSeconds > 0 ? ttlSeconds : JSON_CACHE_DEFAULT_TTL_SEC;
    cache->isInitialized = true;
    pthread_mutex_init(&cache->mutex, NULL);

    LOG_INFO("JsonCache: Initiated with TTL=%d seconds, max entries=%d",
             cache->ttlSeconds, JSON_CACHE_MAX_ENTRIES);
    return 0;
}

void JsonCache_Shutdown(JsonCache *cache)
{
    if (!cache || !cache->isInitialized)
        return;

    pthread_mutex_lock(&cache->mutex);
    memset(cache->entries, 0, sizeof(cache->entries));
    cache->isInitialized = false;
    pthread_mutex_unlock(&cache->mutex);

    pthread_mutex_destroy(&cache->mutex);
    LOG_INFO("JsonCache: Shutdown complete");
}

int JsonCache_Store(JsonCache *cache, const char *key, const char *json)
{
    if (!cache || !cache->isInitialized || !key || !json)
        return -1;

    pthread_mutex_lock(&cache->mutex);

    // Update existing entry if key matches
    for (int i = 0; i < JSON_CACHE_MAX_ENTRIES; i++)
    {
        if (cache->entries[i].occupied && strcmp(cache->entries[i].key, key) == 0)
        {
            strncpy(cache->entries[i].jsonData, json, JSON_CACHE_DATA_MAX - 1);
            cache->entries[i].jsonData[JSON_CACHE_DATA_MAX - 1] = '\0';
            cache->entries[i].createdAt = time(NULL);
            pthread_mutex_unlock(&cache->mutex);
            LOG_INFO("JsonCache: Updated entry for key '%s'", key);
            return 0;
        }
    }

    // Find empty slot or evict oldest entry
    int targetSlot = -1;
    time_t oldestTime = time(NULL);

    for (int i = 0; i < JSON_CACHE_MAX_ENTRIES; i++)
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

    strncpy(cache->entries[targetSlot].key, key, JSON_CACHE_KEY_MAX - 1);
    cache->entries[targetSlot].key[JSON_CACHE_KEY_MAX - 1] = '\0';
    strncpy(cache->entries[targetSlot].jsonData, json, JSON_CACHE_DATA_MAX - 1);
    cache->entries[targetSlot].jsonData[JSON_CACHE_DATA_MAX - 1] = '\0';
    cache->entries[targetSlot].createdAt = time(NULL);
    cache->entries[targetSlot].occupied = true;

    pthread_mutex_unlock(&cache->mutex);
    LOG_INFO("JsonCache: Stored entry for key '%s' (slot %d)", key, targetSlot);
    return 0;
}

int JsonCache_Lookup(JsonCache *cache, const char *key, char *jsonOut, size_t maxLen)
{
    if (!cache || !cache->isInitialized || !key || !jsonOut)
        return -1;

    pthread_mutex_lock(&cache->mutex);

    for (int i = 0; i < JSON_CACHE_MAX_ENTRIES; i++)
    {
        if (!cache->entries[i].occupied)
            continue;

        if (strcmp(cache->entries[i].key, key) != 0)
            continue;

        // Check TTL
        time_t now = time(NULL);
        double age = difftime(now, cache->entries[i].createdAt);
        if (age > cache->ttlSeconds)
        {
            cache->entries[i].occupied = false;
            pthread_mutex_unlock(&cache->mutex);
            LOG_INFO("JsonCache: Entry for '%s' expired (age: %.0fs)", key, age);
            return -1;
        }

        strncpy(jsonOut, cache->entries[i].jsonData, maxLen - 1);
        jsonOut[maxLen - 1] = '\0';
        pthread_mutex_unlock(&cache->mutex);
        LOG_INFO("JsonCache: HIT for '%s' (age: %.0fs)", key, age);
        return 0;
    }

    pthread_mutex_unlock(&cache->mutex);
    LOG_DEBUG("JsonCache: MISS for '%s'", key);
    return -1;
}
