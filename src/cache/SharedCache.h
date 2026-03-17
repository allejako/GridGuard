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

// Initialize or attach to shared memory segment.
// If the segment already exists and is initialized, existing data is kept.
// Returns 0 on success, -1 on error.
int  SharedCache_Initiate(SharedCache *cache, const char *name, int ttlSeconds);

// Store data under key. Evicts oldest entry if full.
// Returns 0 on success, -1 on error.
int  SharedCache_Store(SharedCache *cache, const char *key, const char *data);

// Look up by key. Writes to out on hit.
// Returns 0 on hit, -1 on miss or expired entry.
int  SharedCache_Lookup(SharedCache *cache, const char *key, char *out, size_t maxLen);

// Invalidate a cache entry by key (marks as unoccupied).
// Returns 0 on success, -1 if key not found or error.
int  SharedCache_Invalidate(SharedCache *cache, const char *key);

// Cleanup the shared memory segment completely (Watchdog only).
// Destroys the rwlock and unlinks the shared memory object.
// MUST be called ONLY after all processes using the cache have exited.
// Typically called from Watchdog during final cleanup.
void SharedCache_Cleanup(SharedCache *cache);

// Shutdown this process's access to the shared memory segment.
// Unmaps the region and closes the file descriptor.
// Does NOT destroy the rwlock or unlink - other processes may still be using it.
// Safe to call from any process at any time.
void SharedCache_Shutdown(SharedCache *cache);


#endif // SHARED_CACHE_H
