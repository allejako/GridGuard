#define _POSIX_C_SOURCE 200809L

#include "cache/SharedCache.h"
#include "sys/Logger.h"

#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>

static size_t region_size(void)
{
    return sizeof(SharedCacheRegion);
}

int SharedCache_Initiate(SharedCache *cache, const char *name, int ttlSeconds)
{
    if (!cache || !name) return -1;

    memset(cache, 0, sizeof(SharedCache));
    strncpy(cache->shmName, name, sizeof(cache->shmName) - 1);

    // Open or create the shared memory object
    cache->fd = shm_open(name, O_CREAT | O_RDWR, 0600);
    if (cache->fd < 0)
    {
        LOG_ERROR("SharedCache: shm_open('%s') failed: %s", name, strerror(errno)); return -1;
    }

    // Serialize initialization across processes with an exclusive file lock.
    // Held only during the magic-check + init sequence, then released.
    if (flock(cache->fd, LOCK_EX) < 0)
    {
        LOG_ERROR("SharedCache: flock failed: %s", strerror(errno));
        close(cache->fd);
        shm_unlink(name);
        return -1;
    }

    // Only size the segment if it has not been sized yet.
    struct stat st;
    if (fstat(cache->fd, &st) < 0 || st.st_size < (off_t)region_size())
    {
        if (ftruncate(cache->fd, (off_t)region_size()) < 0)
        {
            LOG_ERROR("SharedCache: ftruncate failed: %s", strerror(errno));
            flock(cache->fd, LOCK_UN);
            close(cache->fd);
            shm_unlink(name);
            return -1;
        }
    }

    // Map the region into this process's address space
    cache->region = mmap(NULL, region_size(), PROT_READ | PROT_WRITE, MAP_SHARED, cache->fd, 0);
    if (cache->region == MAP_FAILED)
    {
        LOG_ERROR("SharedCache: mmap failed: %s", strerror(errno));
        flock(cache->fd, LOCK_UN);
        close(cache->fd);
        shm_unlink(name);
        return -1;
    }

    if (cache->region->magic == SHARED_CACHE_MAGIC)
    {
        // Segment already initialized — reuse existing cached data
        flock(cache->fd, LOCK_UN);
        LOG_INFO("SharedCache: Attached to existing '%s' (%.1f KB, TTL=%ds)", name, (double)region_size() / 1024.0, cache->region->ttlSeconds);
    }
    else
    {
        // First use — initialize the region including the rwlock
        memset(cache->region, 0, region_size());
        cache->region->ttlSeconds = ttlSeconds > 0 ? ttlSeconds : SHARED_CACHE_DEFAULT_TTL;

        // Initialize read-write lock with process-shared attribute
        pthread_rwlockattr_t attr;
        pthread_rwlockattr_init(&attr);
        pthread_rwlockattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

        if (pthread_rwlock_init(&cache->region->rwlock, &attr) != 0)
        {
            LOG_ERROR("SharedCache: pthread_rwlock_init failed: %s", strerror(errno));
            pthread_rwlockattr_destroy(&attr);
            flock(cache->fd, LOCK_UN);
            munmap(cache->region, region_size());
            close(cache->fd);
            shm_unlink(name);
            return -1;
        }
        pthread_rwlockattr_destroy(&attr);

        cache->region->magic = SHARED_CACHE_MAGIC;

        flock(cache->fd, LOCK_UN);
        LOG_INFO("SharedCache: Created '%s' (%.1f KB, TTL=%ds) with process-shared rwlock", name, (double)region_size() / 1024.0, cache->region->ttlSeconds);
    }

    cache->isInitialized = true; return 0;
}

int SharedCache_Store(SharedCache *cache, const char *key, const char *data)
{
    if (!cache || !cache->isInitialized || !key || !data) return -1;

    if (strlen(data) >= SHARED_CACHE_DATA_MAX)
    {
        LOG_ERROR("SharedCache: data too large for key '%s' (%zu bytes, max %d)", key, strlen(data), SHARED_CACHE_DATA_MAX - 1); return -1;
    }

    SharedCacheRegion *r = cache->region;

    // Acquire write lock with 5 second timeout
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5;

    if (pthread_rwlock_timedwrlock(&r->rwlock, &ts) != 0)
    {
        LOG_ERROR("SharedCache: write lock timeout for Store('%s')", key); return -1;
    }

    // Update existing entry if key already present
    for (int i = 0; i < SHARED_CACHE_MAX_ENTRIES; i++)
    {
        if (r->entries[i].occupied && strcmp(r->entries[i].key, key) == 0)
        {
            strncpy(r->entries[i].data, data, SHARED_CACHE_DATA_MAX - 1);
            r->entries[i].data[SHARED_CACHE_DATA_MAX - 1] = '\0';
            r->entries[i].createdAt = time(NULL);
            pthread_rwlock_unlock(&r->rwlock);
            LOG_INFO("SharedCache: Updated '%s'", key); return 0;
        }
    }

    // Find an empty slot; if none, evict the oldest entry
    int    slot   = -1;
    time_t oldest = time(NULL) + 1;

    for (int i = 0; i < SHARED_CACHE_MAX_ENTRIES; i++)
    {
        if (!r->entries[i].occupied)
        {
            slot = i;
            break;
        }
        if (r->entries[i].createdAt < oldest)
        {
            oldest = r->entries[i].createdAt;
            slot   = i;
        }
    }

    strncpy(r->entries[slot].key,  key,  SHARED_CACHE_KEY_MAX  - 1);
    strncpy(r->entries[slot].data, data, SHARED_CACHE_DATA_MAX - 1);
    r->entries[slot].key[SHARED_CACHE_KEY_MAX   - 1] = '\0';
    r->entries[slot].data[SHARED_CACHE_DATA_MAX - 1] = '\0';
    r->entries[slot].createdAt = time(NULL);
    r->entries[slot].occupied  = 1;

    pthread_rwlock_unlock(&r->rwlock);
    LOG_INFO("SharedCache: Stored '%s' (slot %d)", key, slot);
    return 0;
}

int SharedCache_Lookup(SharedCache *cache, const char *key, char *out, size_t maxLen)
{
    if (!cache || !cache->isInitialized || !key || !out) return -1;

    SharedCacheRegion *r = cache->region;

    // Acquire read lock with 5 second timeout
    // Multiple readers can hold this lock simultaneously!!
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5;

    if (pthread_rwlock_timedrdlock(&r->rwlock, &ts) != 0)
    {
        LOG_ERROR("SharedCache: read lock timeout for Lookup('%s')", key); return -1;
    }

    for (int i = 0; i < SHARED_CACHE_MAX_ENTRIES; i++)
    {
        if (!r->entries[i].occupied) continue;

        if (strcmp(r->entries[i].key, key) != 0) continue;

        double age = difftime(time(NULL), r->entries[i].createdAt);
        if (age > r->ttlSeconds)
        {
            // Entry expired - need write lock to modify
            pthread_rwlock_unlock(&r->rwlock);

            // Upgrade to write lock to mark as expired
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 5;
            if (pthread_rwlock_timedwrlock(&r->rwlock, &ts) == 0)
            {
                // Re-validate: entry could have been modified after we released read lock
                if (r->entries[i].occupied && strcmp(r->entries[i].key, key) == 0)
                {
                    double new_age = difftime(time(NULL), r->entries[i].createdAt);
                    if (new_age > r->ttlSeconds)
                    {
                        r->entries[i].occupied = 0;
                    }
                }
                pthread_rwlock_unlock(&r->rwlock);
            }

            LOG_INFO("SharedCache: '%s' expired (%.0fs old)", key, age); return -1;
        }

        strncpy(out, r->entries[i].data, maxLen - 1);
        out[maxLen - 1] = '\0';
        pthread_rwlock_unlock(&r->rwlock);
        LOG_INFO("SharedCache: HIT '%s' (%.0fs old)", key, age); return 0;
    }

    pthread_rwlock_unlock(&r->rwlock);
    LOG_DEBUG("SharedCache: MISS '%s'", key); return -1;
}

int SharedCache_Invalidate(SharedCache *cache, const char *key)
{
    if (!cache || !cache->isInitialized || !key) return -1;

    SharedCacheRegion *r = cache->region;

    // Acquire write lock with 5 second timeout
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5;

    if (pthread_rwlock_timedwrlock(&r->rwlock, &ts) != 0)
    {
        LOG_ERROR("SharedCache: write lock timeout for Invalidate('%s')", key); return -1;
    }

    for (int i = 0; i < SHARED_CACHE_MAX_ENTRIES; i++)
    {
        if (r->entries[i].occupied && strcmp(r->entries[i].key, key) == 0)
        {
            r->entries[i].occupied = 0;
            pthread_rwlock_unlock(&r->rwlock);
            LOG_DEBUG("SharedCache: Invalidated '%s'", key);
            return 0;
        }
    }

    pthread_rwlock_unlock(&r->rwlock);
    LOG_DEBUG("SharedCache: Invalidate('%s') - key not found", key);
    return -1;
}

void SharedCache_Shutdown(SharedCache *cache)
{
    if (!cache || !cache->isInitialized) return;

    // Shutdown this process's access to the shared memory segment.
    // Do NOT destroy the rwlock or unlink - other processes may still be using it!

    munmap(cache->region, region_size());
    close(cache->fd);

    cache->region        = NULL;
    cache->isInitialized = false;

    LOG_INFO("SharedCache: Shutdown '%s' (segment persists for other processes)", cache->shmName);
}

void SharedCache_Cleanup(SharedCache *cache)
{
    if (!cache || !cache->isInitialized) return;

    // CRITICAL: Only call this from Watchdog AFTER all processes have exited!
    //
    // This destroys the rwlock and unlinks the shared memory object.
    // If any process is still using the cache, it will crash with SIGSEGV.

    pthread_rwlock_destroy(&cache->region->rwlock);
    munmap(cache->region, region_size());
    close(cache->fd);
    shm_unlink(cache->shmName);

    cache->region        = NULL;
    cache->isInitialized = false;

    LOG_INFO("SharedCache: Cleanup '%s' (rwlock destroyed, shm unlinked)", cache->shmName);
}
