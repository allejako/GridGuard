#ifndef SHARED_CACHE_H
#define SHARED_CACHE_H

// Process-shared cache using POSIX shared memory and pthread_rwlock.
// Thread-safe and process-safe. 16 entries max, 32KB per entry, LRU eviction.
// Uses mmap() - each process maps at different virtual address.
// Do not store pointers in shared region (invalid across processes).

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <stddef.h>

#define SHARED_CACHE_MAX_ENTRIES  16
#define SHARED_CACHE_KEY_MAX      64
#define SHARED_CACHE_DATA_MAX     32768   // 32 KB — sized for weather JSON
#define SHARED_CACHE_DEFAULT_TTL  900     // 15 minutes
#define SHARED_CACHE_MAGIC        0xCA5EC0DE

// One cached entry — lives inside shared memory
typedef struct
{
    char   key[SHARED_CACHE_KEY_MAX];
    char   data[SHARED_CACHE_DATA_MAX];
    time_t createdAt;
    int    occupied;
} SharedCacheEntry;

// The shared memory region — accessible across processes.
// Must be mmap'd before use; never copy this struct.
typedef struct
{
    unsigned int       magic;       // SHARED_CACHE_MAGIC when initialized
    int                ttlSeconds;
    pthread_rwlock_t   rwlock;      // Read-write lock (process-shared)
    SharedCacheEntry   entries[SHARED_CACHE_MAX_ENTRIES];
} SharedCacheRegion;

// Process-local handle — not in shared memory
typedef struct
{
    SharedCacheRegion *region;    // mmap'd pointer into shared memory
    int                fd;        // shm_open file descriptor
    char               shmName[64];
    bool               isInitialized;
} SharedCache;

// Create or attach to shared memory segment.
// If the segment already exists and is initialized, existing data is kept.
// Returns 0 on success, -1 on error.
int  SharedCache_Create(SharedCache *cache, const char *name, int ttlSeconds);

// Store data under key. Evicts oldest entry if full.
// Returns 0 on success, -1 on error.
int  SharedCache_Store(SharedCache *cache, const char *key, const char *data);

// Look up by key. Writes to out on hit.
// Returns 0 on hit, -1 on miss or expired entry.
int  SharedCache_Lookup(SharedCache *cache, const char *key, char *out, size_t maxLen);

// Unmap and unlink the shared memory segment.
// Call once on server shutdown.
void SharedCache_Destroy(SharedCache *cache);

#endif // SHARED_CACHE_H
