# Hybrid Cache Implementation Guide

**Datum:** 2026-02-18
**Mål:** Implementera en professionell hybrid cache med Shared Memory (L1) + SQLite (L2)

---

## 📋 Innehållsförteckning

1. [Översikt](#översikt)
2. [Mappstruktur](#mappstruktur)
3. [Steg-för-steg implementation](#steg-för-steg-implementation)
4. [Kodexempel](#kodexempel)
5. [Testing](#testing)
6. [Integration](#integration)
7. [Performance](#performance)

---

## Översikt

### Vad är en hybrid cache?

En **två-nivås cache** som kombinerar det bästa av två världar:

```
┌─────────────────────────────────────────────────────────────┐
│                     Cache Request Flow                      │
└─────────────────────────────────────────────────────────────┘

    Request
       │
       ▼
┌──────────────────────┐
│  Level 1: Shared     │  ← 10-50ns (RAM)
│  Memory (Hot Cache)  │  ← 100 entries max
└──────┬───────────────┘  ← Volatile (reset on reboot)
       │
    HIT? ──Yes──→ Return data ⚡⚡⚡
       │
      No
       │
       ▼
┌──────────────────────┐
│  Level 2: SQLite     │  ← 1-10ms (Disk)
│  (Persistent Cache)  │  ← Unlimited entries
└──────┬───────────────┘  ← Persistent (survives reboot)
       │
    HIT? ──Yes──→ Promote to L1 → Return data ⚡⚡
       │
      No
       │
       ▼
┌──────────────────────┐
│  External API        │  ← 100-500ms (Network)
│  (Open-Meteo, etc)   │  ← Source of truth
└──────┬───────────────┘
       │
       ▼
   Save to L1 + L2 → Return data ⚡
```

### Fördelar

| Feature | Shared Memory (L1) | SQLite (L2) | Hybrid (L1+L2) |
|---------|-------------------|-------------|----------------|
| **Speed** | ⚡⚡⚡ (ns) | ⚡⚡ (ms) | ⚡⚡⚡ (hot) + ⚡⚡ (warm) |
| **Persistence** | ❌ | ✅ | ✅ |
| **Capacity** | Limited (100 entries) | Unlimited | Best of both |
| **Cross-process** | ✅ | ✅ | ✅ |
| **After restart** | ❌ Empty | ✅ Full | ✅ Auto-repopulate L1 |

---

## Mappstruktur

### Ny struktur (Rekommenderad)

```
src/
├── infrastructure/
│   ├── cache/                    # ← NY MAPP för cache
│   │   ├── HybridCache.h         # Public API (L1 + L2 combined)
│   │   ├── HybridCache.c
│   │   ├── SharedCache.h         # Level 1 implementation (private)
│   │   ├── SharedCache.c
│   │   ├── SQLiteCache.h         # Level 2 implementation (private)
│   │   └── SQLiteCache.c
│   │
│   ├── database/                 # Generell database-funktionalitet
│   │   ├── Database.h            # SQLite wrapper (connection, queries)
│   │   └── Database.c
│   │
│   └── logging/
│       ├── Logger.h
│       └── Logger.c
│
├── application/
│   └── workers/
│       ├── FetchWorker.c         # Använder HybridCache
│       └── ...
│
├── tests/
│   ├── unit/
│   │   ├── test_shared_cache.c   # Test L1
│   │   ├── test_sqlite_cache.c   # Test L2
│   │   └── test_hybrid_cache.c   # Test L1+L2
│   └── integration/
│       └── test_cache_integration.c
│
└── data/
    └── gridguard.db              # SQLite database file
```

### Varför denna struktur?

1. **`infrastructure/cache/`** - Samlar ALL cache-logik
   - HybridCache.h är det enda publika API:et
   - SharedCache och SQLiteCache är interna implementationsdetaljer

2. **`infrastructure/database/`** - Generella databas-utilities
   - Kan återanvändas för user configs, request logs, etc.
   - Separation of concerns

3. **Testbarhet** - Varje nivå kan testas isolerat

---

## Steg-för-steg Implementation

### Fas 1: Foundation (Database wrapper)

**Mål:** Skapa en robust SQLite wrapper

#### Steg 1.1: Skapa infrastructure/database/Database.h

```c
/**
 * @file Database.h
 * @brief SQLite database wrapper - generell databas-funktionalitet
 *
 * Tillhandahåller grundläggande SQLite-operationer som kan användas
 * av flera moduler (cache, user configs, logs).
 */

#ifndef DATABASE_H
#define DATABASE_H

#include <sqlite3.h>
#include <stdbool.h>

/**
 * @brief Database connection handle
 */
typedef struct {
    sqlite3 *db;           ///< SQLite database handle
    char dbPath[256];      ///< Path to database file
    bool isOpen;           ///< Connection status
} Database;

/**
 * @brief Initialize and open database connection
 *
 * @param db        Pointer to Database structure
 * @param dbPath    Path to SQLite database file (will be created if not exists)
 * @return 0 on success, -1 on error
 *
 * @example
 *   Database db;
 *   if (Database_Init(&db, "data/gridguard.db") == 0) {
 *       // Database ready to use
 *   }
 */
int Database_Init(Database *db, const char *dbPath);

/**
 * @brief Execute SQL statement without result
 *
 * @param db    Database handle
 * @param sql   SQL statement to execute
 * @return 0 on success, -1 on error
 *
 * @example
 *   Database_Exec(&db, "CREATE TABLE IF NOT EXISTS cache (id INTEGER PRIMARY KEY)");
 */
int Database_Exec(Database *db, const char *sql);

/**
 * @brief Close database connection
 *
 * @param db Database handle
 */
void Database_Close(Database *db);

/**
 * @brief Get last error message
 *
 * @param db Database handle
 * @return Error message string (valid until next SQLite call)
 */
const char* Database_GetError(Database *db);

#endif // DATABASE_H
```

#### Steg 1.2: Implementera infrastructure/database/Database.c

```c
/**
 * @file Database.c
 * @brief SQLite database wrapper implementation
 */

#include "Database.h"
#include "../logging/Logger.h"
#include <string.h>

int Database_Init(Database *db, const char *dbPath) {
    if (!db || !dbPath) {
        LOG_ERROR("Database_Init: Invalid parameters");
        return -1;
    }

    memset(db, 0, sizeof(Database));
    strncpy(db->dbPath, dbPath, sizeof(db->dbPath) - 1);

    // Open database (creates if not exists)
    int rc = sqlite3_open(dbPath, &db->db);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to open database '%s': %s",
                  dbPath, sqlite3_errmsg(db->db));
        return -1;
    }

    // Enable foreign keys
    Database_Exec(db, "PRAGMA foreign_keys = ON");

    // Set journal mode to WAL for better concurrency
    Database_Exec(db, "PRAGMA journal_mode = WAL");

    db->isOpen = true;
    LOG_INFO("Database opened: %s", dbPath);

    return 0;
}

int Database_Exec(Database *db, const char *sql) {
    if (!db || !db->isOpen || !sql) {
        LOG_ERROR("Database_Exec: Invalid parameters");
        return -1;
    }

    char *errMsg = NULL;
    int rc = sqlite3_exec(db->db, sql, NULL, NULL, &errMsg);

    if (rc != SQLITE_OK) {
        LOG_ERROR("SQL execution failed: %s\nSQL: %s", errMsg, sql);
        sqlite3_free(errMsg);
        return -1;
    }

    return 0;
}

void Database_Close(Database *db) {
    if (db && db->isOpen) {
        sqlite3_close(db->db);
        db->isOpen = false;
        LOG_INFO("Database closed: %s", db->dbPath);
    }
}

const char* Database_GetError(Database *db) {
    if (db && db->db) {
        return sqlite3_errmsg(db->db);
    }
    return "Database not initialized";
}
```

---

### Fas 2: Level 1 Cache (Shared Memory)

**Mål:** Implementera snabb shared memory cache

#### Steg 2.1: Skapa infrastructure/cache/SharedCache.h

```c
/**
 * @file SharedCache.h
 * @brief Level 1 Cache - POSIX Shared Memory implementation
 *
 * Snabb in-memory cache som delas mellan processer via shared memory.
 * Används som första nivå i hybrid cache för maximal prestanda.
 *
 * Prestanda: ~10-50 nanosekunder per lookup
 * Kapacitet: Begränsad (SHARED_CACHE_MAX_ENTRIES)
 * Persistence: NEJ - försvinner vid system reboot
 */

#ifndef SHARED_CACHE_H
#define SHARED_CACHE_H

#include <pthread.h>
#include <time.h>
#include <stdbool.h>

// Configuration
#define SHARED_CACHE_MAX_ENTRIES 100        ///< Max antal cachade entries
#define SHARED_CACHE_KEY_SIZE 64            ///< Max längd på cache key
#define SHARED_CACHE_JSON_SIZE 8192         ///< Max JSON size (8KB)
#define SHARED_CACHE_NAME "/gridguard_cache" ///< Shared memory name

/**
 * @brief En enskild cache entry
 */
typedef struct {
    char key[SHARED_CACHE_KEY_SIZE];        ///< Cache key (t.ex. "59.33,18.07")
    char data[SHARED_CACHE_JSON_SIZE];      ///< Cached JSON data
    time_t expiresAt;                       ///< Expiration timestamp
    bool valid;                             ///< Entry är giltig
    time_t lastAccessed;                    ///< För LRU eviction
} SharedCacheEntry;

/**
 * @brief Shared memory cache data structure
 *
 * Denna struktur mappas direkt till shared memory via mmap().
 * Alla processer delar samma fysiska minne.
 */
typedef struct {
    pthread_mutex_t mutex;                  ///< Process-shared mutex för thread-safety
    int numEntries;                         ///< Antal entries i bruk
    unsigned long hits;                     ///< Antal cache hits (statistik)
    unsigned long misses;                   ///< Antal cache misses
    SharedCacheEntry entries[SHARED_CACHE_MAX_ENTRIES];
} SharedCacheData;

/**
 * @brief Initialisera shared memory cache
 *
 * Skapar eller öppnar shared memory segment och initialiserar mutex.
 *
 * @param cacheOut Pointer till cache-pekare (allokeras av funktionen)
 * @return 0 on success, -1 on error
 *
 * @note Första processen som anropar detta skapar shared memory,
 *       efterföljande processer kopplar till befintligt segment.
 */
int SharedCache_Init(SharedCacheData **cacheOut);

/**
 * @brief Hämta data från cache
 *
 * @param cache     Cache handle
 * @param key       Cache key (t.ex. "59.33,18.07" för weather)
 * @param dataOut   Buffer för cached data (måste vara minst SHARED_CACHE_JSON_SIZE)
 * @param now       Nuvarande tid (för expiry check)
 * @return 0 on HIT, -1 on MISS
 *
 * @example
 *   char buffer[SHARED_CACHE_JSON_SIZE];
 *   if (SharedCache_Get(cache, "59.33,18.07", buffer, time(NULL)) == 0) {
 *       printf("Cache HIT: %s\n", buffer);
 *   }
 */
int SharedCache_Get(SharedCacheData *cache, const char *key,
                    char *dataOut, time_t now);

/**
 * @brief Sätt data i cache
 *
 * @param cache     Cache handle
 * @param key       Cache key
 * @param data      Data att cacha (JSON string)
 * @param expiresAt Expiration timestamp
 * @return 0 on success, -1 on error
 *
 * @note Om cache är full används LRU eviction (Least Recently Used)
 */
int SharedCache_Set(SharedCacheData *cache, const char *key,
                    const char *data, time_t expiresAt);

/**
 * @brief Ta bort utgångna entries
 *
 * @param cache Cache handle
 * @param now   Nuvarande tid
 * @return Antal borttagna entries
 */
int SharedCache_CleanupExpired(SharedCacheData *cache, time_t now);

/**
 * @brief Hämta cache-statistik
 *
 * @param cache     Cache handle
 * @param hitsOut   Antal hits (optional, kan vara NULL)
 * @param missesOut Antal misses (optional, kan vara NULL)
 */
void SharedCache_GetStats(SharedCacheData *cache,
                          unsigned long *hitsOut,
                          unsigned long *missesOut);

/**
 * @brief Stäng och frigör shared memory
 *
 * @param cache Cache handle
 *
 * @warning Detta tar BORT shared memory segmentet helt!
 *          Anropa bara från sista processen som stänger ner.
 */
void SharedCache_Destroy(SharedCacheData *cache);

#endif // SHARED_CACHE_H
```

#### Steg 2.2: Implementera infrastructure/cache/SharedCache.c

```c
/**
 * @file SharedCache.c
 * @brief Shared Memory Cache implementation
 */

#include "SharedCache.h"
#include "../logging/Logger.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

int SharedCache_Init(SharedCacheData **cacheOut) {
    if (!cacheOut) {
        LOG_ERROR("SharedCache_Init: Invalid parameter");
        return -1;
    }

    // Create or open shared memory segment
    int fd = shm_open(SHARED_CACHE_NAME, O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        LOG_ERROR("shm_open failed: %s", strerror(errno));
        return -1;
    }

    // Set size
    if (ftruncate(fd, sizeof(SharedCacheData)) != 0) {
        LOG_ERROR("ftruncate failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    // Map to memory
    SharedCacheData *cache = mmap(NULL, sizeof(SharedCacheData),
                                   PROT_READ | PROT_WRITE,
                                   MAP_SHARED, fd, 0);
    close(fd); // Can close fd after mmap

    if (cache == MAP_FAILED) {
        LOG_ERROR("mmap failed: %s", strerror(errno));
        return -1;
    }

    // Initialize process-shared mutex (only first time)
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&cache->mutex, &attr);
    pthread_mutexattr_destroy(&attr);

    // Initialize stats (if first time)
    if (cache->numEntries == 0) {
        cache->hits = 0;
        cache->misses = 0;
    }

    *cacheOut = cache;

    LOG_INFO("Shared memory cache initialized (%zu bytes, %d max entries)",
             sizeof(SharedCacheData), SHARED_CACHE_MAX_ENTRIES);

    return 0;
}

int SharedCache_Get(SharedCacheData *cache, const char *key,
                    char *dataOut, time_t now) {
    if (!cache || !key || !dataOut) {
        return -1;
    }

    pthread_mutex_lock(&cache->mutex);

    // Linear search through entries
    for (int i = 0; i < cache->numEntries; i++) {
        SharedCacheEntry *entry = &cache->entries[i];

        if (entry->valid &&
            strcmp(entry->key, key) == 0 &&
            entry->expiresAt > now) {

            // HIT - copy data out
            strncpy(dataOut, entry->data, SHARED_CACHE_JSON_SIZE - 1);
            dataOut[SHARED_CACHE_JSON_SIZE - 1] = '\0';

            entry->lastAccessed = now; // Update for LRU
            cache->hits++;

            pthread_mutex_unlock(&cache->mutex);
            return 0; // HIT
        }
    }

    // MISS
    cache->misses++;
    pthread_mutex_unlock(&cache->mutex);
    return -1;
}

int SharedCache_Set(SharedCacheData *cache, const char *key,
                    const char *data, time_t expiresAt) {
    if (!cache || !key || !data) {
        return -1;
    }

    pthread_mutex_lock(&cache->mutex);

    int slot = -1;

    // 1. Try to find existing entry with same key
    for (int i = 0; i < cache->numEntries; i++) {
        if (strcmp(cache->entries[i].key, key) == 0) {
            slot = i;
            break;
        }
    }

    // 2. If not found, use new slot or evict oldest
    if (slot == -1) {
        if (cache->numEntries < SHARED_CACHE_MAX_ENTRIES) {
            // Use new slot
            slot = cache->numEntries++;
        } else {
            // Cache full - evict least recently used (LRU)
            time_t oldestTime = cache->entries[0].lastAccessed;
            slot = 0;

            for (int i = 1; i < SHARED_CACHE_MAX_ENTRIES; i++) {
                if (cache->entries[i].lastAccessed < oldestTime) {
                    oldestTime = cache->entries[i].lastAccessed;
                    slot = i;
                }
            }

            LOG_DEBUG("Cache full, evicting entry: %s", cache->entries[slot].key);
        }
    }

    // 3. Write entry
    SharedCacheEntry *entry = &cache->entries[slot];
    strncpy(entry->key, key, SHARED_CACHE_KEY_SIZE - 1);
    entry->key[SHARED_CACHE_KEY_SIZE - 1] = '\0';

    strncpy(entry->data, data, SHARED_CACHE_JSON_SIZE - 1);
    entry->data[SHARED_CACHE_JSON_SIZE - 1] = '\0';

    entry->expiresAt = expiresAt;
    entry->lastAccessed = time(NULL);
    entry->valid = true;

    pthread_mutex_unlock(&cache->mutex);
    return 0;
}

int SharedCache_CleanupExpired(SharedCacheData *cache, time_t now) {
    if (!cache) return -1;

    pthread_mutex_lock(&cache->mutex);

    int removed = 0;
    for (int i = 0; i < cache->numEntries; i++) {
        if (cache->entries[i].valid && cache->entries[i].expiresAt < now) {
            cache->entries[i].valid = false;
            removed++;
        }
    }

    pthread_mutex_unlock(&cache->mutex);

    if (removed > 0) {
        LOG_DEBUG("Cleaned up %d expired entries from shared cache", removed);
    }

    return removed;
}

void SharedCache_GetStats(SharedCacheData *cache,
                          unsigned long *hitsOut,
                          unsigned long *missesOut) {
    if (!cache) return;

    pthread_mutex_lock(&cache->mutex);

    if (hitsOut) *hitsOut = cache->hits;
    if (missesOut) *missesOut = cache->misses;

    pthread_mutex_unlock(&cache->mutex);
}

void SharedCache_Destroy(SharedCacheData *cache) {
    if (!cache) return;

    pthread_mutex_destroy(&cache->mutex);
    munmap(cache, sizeof(SharedCacheData));
    shm_unlink(SHARED_CACHE_NAME);

    LOG_INFO("Shared memory cache destroyed");
}
```

---

### Fas 3: Level 2 Cache (SQLite)

#### Steg 3.1: Skapa infrastructure/cache/SQLiteCache.h

```c
/**
 * @file SQLiteCache.h
 * @brief Level 2 Cache - SQLite persistent storage
 *
 * Disk-baserad cache med obegränsad kapacitet och persistence.
 * Används som andra nivå i hybrid cache för långtidslagring.
 *
 * Prestanda: ~1-10 millisekunder per lookup
 * Kapacitet: Praktiskt obegränsad (140TB teoretiskt)
 * Persistence: JA - överlever system reboot
 */

#ifndef SQLITE_CACHE_H
#define SQLITE_CACHE_H

#include "../database/Database.h"
#include <time.h>

/**
 * @brief SQLite cache handle
 */
typedef struct {
    Database db;           ///< Database connection
    bool isInitialized;    ///< Initialization status
} SQLiteCache;

/**
 * @brief Initialize SQLite cache
 *
 * Skapar tabeller om de inte existerar.
 *
 * @param cache     Cache handle
 * @param dbPath    Path to database file
 * @return 0 on success, -1 on error
 */
int SQLiteCache_Init(SQLiteCache *cache, const char *dbPath);

/**
 * @brief Get weather data from cache
 *
 * @param cache     Cache handle
 * @param lat       Latitude
 * @param lon       Longitude
 * @param jsonOut   Pointer to allocated string (caller must free)
 * @return 0 on HIT, -1 on MISS
 */
int SQLiteCache_GetWeather(SQLiteCache *cache, double lat, double lon,
                           char **jsonOut);

/**
 * @brief Set weather data in cache
 *
 * @param cache         Cache handle
 * @param lat           Latitude
 * @param lon           Longitude
 * @param json          JSON data to cache
 * @param ttlSeconds    Time-to-live in seconds
 * @return 0 on success, -1 on error
 */
int SQLiteCache_SetWeather(SQLiteCache *cache, double lat, double lon,
                           const char *json, int ttlSeconds);

/**
 * @brief Get spot price data from cache
 *
 * @param cache     Cache handle
 * @param region    Region code (SE1, SE2, SE3, SE4)
 * @param date      Date string (YYYY-MM-DD)
 * @param jsonOut   Pointer to allocated string (caller must free)
 * @return 0 on HIT, -1 on MISS
 */
int SQLiteCache_GetPrices(SQLiteCache *cache, const char *region,
                          const char *date, char **jsonOut);

/**
 * @brief Set spot price data in cache
 *
 * @param cache     Cache handle
 * @param region    Region code
 * @param date      Date string
 * @param json      JSON data to cache
 * @return 0 on success, -1 on error
 */
int SQLiteCache_SetPrices(SQLiteCache *cache, const char *region,
                          const char *date, const char *json);

/**
 * @brief Remove expired entries
 *
 * @param cache Cache handle
 * @return Number of removed entries, -1 on error
 */
int SQLiteCache_CleanupExpired(SQLiteCache *cache);

/**
 * @brief Close cache and database
 *
 * @param cache Cache handle
 */
void SQLiteCache_Close(SQLiteCache *cache);

#endif // SQLITE_CACHE_H
```

#### Steg 3.2: Implementera infrastructure/cache/SQLiteCache.c

```c
/**
 * @file SQLiteCache.c
 * @brief SQLite persistent cache implementation
 */

#include "SQLiteCache.h"
#include "../logging/Logger.h"
#include <string.h>
#include <stdlib.h>

// SQL schema
static const char *CREATE_TABLES_SQL =
    "CREATE TABLE IF NOT EXISTS weather_cache ("
    "  location_key TEXT PRIMARY KEY,"
    "  latitude REAL NOT NULL,"
    "  longitude REAL NOT NULL,"
    "  forecast_json TEXT NOT NULL,"
    "  fetched_at INTEGER NOT NULL,"
    "  expires_at INTEGER NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_weather_expires "
    "  ON weather_cache(expires_at);"
    ""
    "CREATE TABLE IF NOT EXISTS price_cache ("
    "  region TEXT NOT NULL,"
    "  date TEXT NOT NULL,"
    "  prices_json TEXT NOT NULL,"
    "  fetched_at INTEGER NOT NULL,"
    "  PRIMARY KEY (region, date)"
    ");";

int SQLiteCache_Init(SQLiteCache *cache, const char *dbPath) {
    if (!cache || !dbPath) {
        LOG_ERROR("SQLiteCache_Init: Invalid parameters");
        return -1;
    }

    memset(cache, 0, sizeof(SQLiteCache));

    // Initialize database
    if (Database_Init(&cache->db, dbPath) != 0) {
        return -1;
    }

    // Create tables
    if (Database_Exec(&cache->db, CREATE_TABLES_SQL) != 0) {
        Database_Close(&cache->db);
        return -1;
    }

    cache->isInitialized = true;
    LOG_INFO("SQLite cache initialized: %s", dbPath);

    return 0;
}

int SQLiteCache_GetWeather(SQLiteCache *cache, double lat, double lon,
                           char **jsonOut) {
    if (!cache || !cache->isInitialized || !jsonOut) {
        return -1;
    }

    char locationKey[64];
    snprintf(locationKey, sizeof(locationKey), "%.2f,%.2f", lat, lon);

    const char *sql =
        "SELECT forecast_json FROM weather_cache "
        "WHERE location_key = ? AND expires_at > ?";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(cache->db.db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOG_ERROR("SQLite prepare failed: %s", sqlite3_errmsg(cache->db.db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, locationKey, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, time(NULL));

    int result = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *json = (const char*)sqlite3_column_text(stmt, 0);
        *jsonOut = strdup(json);
        result = 0; // HIT
    }

    sqlite3_finalize(stmt);
    return result;
}

int SQLiteCache_SetWeather(SQLiteCache *cache, double lat, double lon,
                           const char *json, int ttlSeconds) {
    if (!cache || !cache->isInitialized || !json) {
        return -1;
    }

    char locationKey[64];
    snprintf(locationKey, sizeof(locationKey), "%.2f,%.2f", lat, lon);

    time_t now = time(NULL);
    time_t expiresAt = now + ttlSeconds;

    const char *sql =
        "INSERT OR REPLACE INTO weather_cache "
        "(location_key, latitude, longitude, forecast_json, fetched_at, expires_at) "
        "VALUES (?, ?, ?, ?, ?, ?)";

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(cache->db.db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, locationKey, -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 2, lat);
    sqlite3_bind_double(stmt, 3, lon);
    sqlite3_bind_text(stmt, 4, json, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 5, now);
    sqlite3_bind_int64(stmt, 6, expiresAt);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

int SQLiteCache_GetPrices(SQLiteCache *cache, const char *region,
                          const char *date, char **jsonOut) {
    if (!cache || !cache->isInitialized || !region || !date || !jsonOut) {
        return -1;
    }

    const char *sql =
        "SELECT prices_json FROM price_cache "
        "WHERE region = ? AND date = ?";

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(cache->db.db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, region, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, date, -1, SQLITE_STATIC);

    int result = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *json = (const char*)sqlite3_column_text(stmt, 0);
        *jsonOut = strdup(json);
        result = 0;
    }

    sqlite3_finalize(stmt);
    return result;
}

int SQLiteCache_SetPrices(SQLiteCache *cache, const char *region,
                          const char *date, const char *json) {
    if (!cache || !cache->isInitialized || !region || !date || !json) {
        return -1;
    }

    const char *sql =
        "INSERT OR REPLACE INTO price_cache "
        "(region, date, prices_json, fetched_at) "
        "VALUES (?, ?, ?, ?)";

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(cache->db.db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, region, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, date, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, json, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 4, time(NULL));

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

int SQLiteCache_CleanupExpired(SQLiteCache *cache) {
    if (!cache || !cache->isInitialized) {
        return -1;
    }

    const char *sql = "DELETE FROM weather_cache WHERE expires_at < ?";

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(cache->db.db, sql, -1, &stmt, NULL);
    sqlite3_bind_int64(stmt, 1, time(NULL));
    sqlite3_step(stmt);

    int deleted = sqlite3_changes(cache->db.db);
    sqlite3_finalize(stmt);

    LOG_INFO("SQLite cleanup: Removed %d expired entries", deleted);

    return deleted;
}

void SQLiteCache_Close(SQLiteCache *cache) {
    if (cache && cache->isInitialized) {
        Database_Close(&cache->db);
        cache->isInitialized = false;
    }
}
```

---

### Fas 4: Hybrid Cache (L1 + L2 Combined)

#### Steg 4.1: Skapa infrastructure/cache/HybridCache.h

```c
/**
 * @file HybridCache.h
 * @brief Hybrid Cache - Combines Shared Memory (L1) + SQLite (L2)
 *
 * PUBLIC API för cache-systemet. Detta är det enda interface
 * som FetchWorker och andra moduler ska använda.
 *
 * Prestanda:
 *  - L1 hit: ~10-50ns (hot data)
 *  - L2 hit: ~1-10ms (warm data, promotes to L1)
 *  - Miss:   ~100-500ms (API fetch, saves to both L1+L2)
 */

#ifndef HYBRID_CACHE_H
#define HYBRID_CACHE_H

#include "SharedCache.h"
#include "SQLiteCache.h"
#include <stdbool.h>

/**
 * @brief Cache level enum for statistics
 */
typedef enum {
    CACHE_LEVEL_NONE = 0,           ///< Cache miss
    CACHE_LEVEL_SHARED_MEMORY = 1,  ///< L1 hit
    CACHE_LEVEL_SQLITE = 2          ///< L2 hit
} CacheLevel;

/**
 * @brief Cache statistics
 */
typedef struct {
    unsigned long l1Hits;       ///< Shared memory hits
    unsigned long l2Hits;       ///< SQLite hits
    unsigned long misses;       ///< Total misses
    unsigned long promotions;   ///< L2 → L1 promotions
    double l1HitRate;           ///< L1 hit rate (%)
    double l2HitRate;           ///< L2 hit rate (%)
    double totalHitRate;        ///< Total hit rate (%)
} CacheStats;

/**
 * @brief Hybrid cache handle
 */
typedef struct {
    SharedCacheData *sharedCache;  ///< Level 1 (shared memory)
    SQLiteCache sqliteCache;       ///< Level 2 (SQLite)

    // Statistics
    unsigned long l1Hits;
    unsigned long l2Hits;
    unsigned long misses;
    unsigned long promotions;

    bool isInitialized;
} HybridCache;

/**
 * @brief Initialize hybrid cache
 *
 * @param cache     Cache handle
 * @param dbPath    Path to SQLite database file
 * @return 0 on success, -1 on error
 *
 * @example
 *   HybridCache cache;
 *   if (HybridCache_Init(&cache, "data/gridguard.db") == 0) {
 *       // Cache ready
 *   }
 */
int HybridCache_Init(HybridCache *cache, const char *dbPath);

/**
 * @brief Get weather data from cache (checks L1 → L2 → MISS)
 *
 * Flow:
 *  1. Check L1 (shared memory) - if HIT, return immediately
 *  2. Check L2 (SQLite) - if HIT, promote to L1, then return
 *  3. Return MISS - caller must fetch from API
 *
 * @param cache     Cache handle
 * @param lat       Latitude
 * @param lon       Longitude
 * @param jsonOut   Pointer to allocated string (caller must free if HIT)
 * @param levelOut  Which level hit (optional, can be NULL)
 * @return 0 on HIT, -1 on MISS
 *
 * @example
 *   char *json = NULL;
 *   CacheLevel level;
 *   if (HybridCache_GetWeather(&cache, 59.33, 18.07, &json, &level) == 0) {
 *       printf("Cache HIT from level %d: %s\n", level, json);
 *       free(json);
 *   } else {
 *       // Fetch from API, then call HybridCache_SetWeather()
 *   }
 */
int HybridCache_GetWeather(HybridCache *cache, double lat, double lon,
                           char **jsonOut, CacheLevel *levelOut);

/**
 * @brief Set weather data in cache (writes to BOTH L1 and L2)
 *
 * @param cache         Cache handle
 * @param lat           Latitude
 * @param lon           Longitude
 * @param json          JSON data to cache
 * @param ttlSeconds    Time-to-live in seconds (typically 900 = 15min)
 * @return 0 on success, -1 on error
 */
int HybridCache_SetWeather(HybridCache *cache, double lat, double lon,
                           const char *json, int ttlSeconds);

/**
 * @brief Get spot price data from cache
 *
 * @param cache     Cache handle
 * @param region    Region code (SE1, SE2, SE3, SE4)
 * @param date      Date string (YYYY-MM-DD)
 * @param jsonOut   Pointer to allocated string (caller must free)
 * @param levelOut  Which level hit (optional)
 * @return 0 on HIT, -1 on MISS
 */
int HybridCache_GetPrices(HybridCache *cache, const char *region,
                          const char *date, char **jsonOut,
                          CacheLevel *levelOut);

/**
 * @brief Set spot price data in cache
 *
 * @param cache     Cache handle
 * @param region    Region code
 * @param date      Date string
 * @param json      JSON data
 * @return 0 on success, -1 on error
 */
int HybridCache_SetPrices(HybridCache *cache, const char *region,
                          const char *date, const char *json);

/**
 * @brief Cleanup expired entries (both L1 and L2)
 *
 * @param cache Cache handle
 * @return Total number of removed entries
 */
int HybridCache_CleanupExpired(HybridCache *cache);

/**
 * @brief Get cache statistics
 *
 * @param cache     Cache handle
 * @param statsOut  Statistics output
 */
void HybridCache_GetStats(HybridCache *cache, CacheStats *statsOut);

/**
 * @brief Close and cleanup cache
 *
 * @param cache Cache handle
 *
 * @warning Does NOT destroy shared memory (other processes may use it).
 *          Call SharedCache_Destroy() separately if needed.
 */
void HybridCache_Close(HybridCache *cache);

#endif // HYBRID_CACHE_H
```

#### Steg 4.2: Implementera infrastructure/cache/HybridCache.c

```c
/**
 * @file HybridCache.c
 * @brief Hybrid cache implementation - L1 + L2 orchestration
 */

#include "HybridCache.h"
#include "../logging/Logger.h"
#include <string.h>
#include <stdlib.h>

int HybridCache_Init(HybridCache *cache, const char *dbPath) {
    if (!cache || !dbPath) {
        LOG_ERROR("HybridCache_Init: Invalid parameters");
        return -1;
    }

    memset(cache, 0, sizeof(HybridCache));

    // Initialize Level 1 (Shared Memory)
    if (SharedCache_Init(&cache->sharedCache) != 0) {
        LOG_ERROR("Failed to initialize shared memory cache");
        return -1;
    }

    // Initialize Level 2 (SQLite)
    if (SQLiteCache_Init(&cache->sqliteCache, dbPath) != 0) {
        LOG_ERROR("Failed to initialize SQLite cache");
        SharedCache_Destroy(cache->sharedCache);
        return -1;
    }

    cache->isInitialized = true;
    LOG_INFO("Hybrid cache initialized (L1: Shared Memory, L2: SQLite)");

    return 0;
}

int HybridCache_GetWeather(HybridCache *cache, double lat, double lon,
                           char **jsonOut, CacheLevel *levelOut) {
    if (!cache || !cache->isInitialized || !jsonOut) {
        return -1;
    }

    char locationKey[64];
    snprintf(locationKey, sizeof(locationKey), "%.2f,%.2f", lat, lon);
    time_t now = time(NULL);

    // ═══════════════════════════════════════════════════════
    // LEVEL 1: Check Shared Memory (fastest)
    // ═══════════════════════════════════════════════════════
    char tempBuffer[SHARED_CACHE_JSON_SIZE];
    if (SharedCache_Get(cache->sharedCache, locationKey, tempBuffer, now) == 0) {
        *jsonOut = strdup(tempBuffer);
        cache->l1Hits++;

        if (levelOut) *levelOut = CACHE_LEVEL_SHARED_MEMORY;

        LOG_DEBUG("Cache L1 HIT (shared memory): %s", locationKey);
        return 0;
    }

    // ═══════════════════════════════════════════════════════
    // LEVEL 2: Check SQLite (slower, but persistent)
    // ═══════════════════════════════════════════════════════
    char *sqliteData = NULL;
    if (SQLiteCache_GetWeather(&cache->sqliteCache, lat, lon, &sqliteData) == 0) {
        cache->l2Hits++;

        if (levelOut) *levelOut = CACHE_LEVEL_SQLITE;

        LOG_DEBUG("Cache L2 HIT (SQLite): %s", locationKey);

        // ═══════════════════════════════════════════════════
        // PROMOTE to Level 1 for future hits
        // ═══════════════════════════════════════════════════
        time_t expiresAt = now + 900; // 15 minutes (default TTL)
        SharedCache_Set(cache->sharedCache, locationKey, sqliteData, expiresAt);
        cache->promotions++;

        LOG_DEBUG("Promoted to L1: %s", locationKey);

        *jsonOut = sqliteData; // Transfer ownership to caller
        return 0;
    }

    // ═══════════════════════════════════════════════════════
    // MISS - Not in L1 or L2
    // ═══════════════════════════════════════════════════════
    cache->misses++;

    if (levelOut) *levelOut = CACHE_LEVEL_NONE;

    LOG_DEBUG("Cache MISS (both levels): %s", locationKey);
    return -1;
}

int HybridCache_SetWeather(HybridCache *cache, double lat, double lon,
                           const char *json, int ttlSeconds) {
    if (!cache || !cache->isInitialized || !json) {
        return -1;
    }

    char locationKey[64];
    snprintf(locationKey, sizeof(locationKey), "%.2f,%.2f", lat, lon);
    time_t expiresAt = time(NULL) + ttlSeconds;

    // ═══════════════════════════════════════════════════════
    // Write to BOTH levels simultaneously
    // ═══════════════════════════════════════════════════════

    // Write to L1 (shared memory)
    SharedCache_Set(cache->sharedCache, locationKey, json, expiresAt);

    // Write to L2 (SQLite) - persistent
    if (SQLiteCache_SetWeather(&cache->sqliteCache, lat, lon, json, ttlSeconds) != 0) {
        LOG_ERROR("Failed to write to SQLite cache");
        return -1;
    }

    LOG_DEBUG("Cache SET (both levels): %s", locationKey);
    return 0;
}

int HybridCache_GetPrices(HybridCache *cache, const char *region,
                          const char *date, char **jsonOut,
                          CacheLevel *levelOut) {
    if (!cache || !cache->isInitialized || !region || !date || !jsonOut) {
        return -1;
    }

    char priceKey[128];
    snprintf(priceKey, sizeof(priceKey), "price_%s_%s", region, date);
    time_t now = time(NULL);

    // L1 check
    char tempBuffer[SHARED_CACHE_JSON_SIZE];
    if (SharedCache_Get(cache->sharedCache, priceKey, tempBuffer, now) == 0) {
        *jsonOut = strdup(tempBuffer);
        cache->l1Hits++;
        if (levelOut) *levelOut = CACHE_LEVEL_SHARED_MEMORY;
        return 0;
    }

    // L2 check
    char *sqliteData = NULL;
    if (SQLiteCache_GetPrices(&cache->sqliteCache, region, date, &sqliteData) == 0) {
        cache->l2Hits++;
        if (levelOut) *levelOut = CACHE_LEVEL_SQLITE;

        // Promote to L1 (prices don't expire, use 1 day)
        time_t expiresAt = now + 86400;
        SharedCache_Set(cache->sharedCache, priceKey, sqliteData, expiresAt);
        cache->promotions++;

        *jsonOut = sqliteData;
        return 0;
    }

    // Miss
    cache->misses++;
    if (levelOut) *levelOut = CACHE_LEVEL_NONE;
    return -1;
}

int HybridCache_SetPrices(HybridCache *cache, const char *region,
                          const char *date, const char *json) {
    if (!cache || !cache->isInitialized || !region || !date || !json) {
        return -1;
    }

    char priceKey[128];
    snprintf(priceKey, sizeof(priceKey), "price_%s_%s", region, date);
    time_t expiresAt = time(NULL) + 86400; // 1 day

    // Write to both levels
    SharedCache_Set(cache->sharedCache, priceKey, json, expiresAt);

    if (SQLiteCache_SetPrices(&cache->sqliteCache, region, date, json) != 0) {
        LOG_ERROR("Failed to write prices to SQLite");
        return -1;
    }

    return 0;
}

int HybridCache_CleanupExpired(HybridCache *cache) {
    if (!cache || !cache->isInitialized) {
        return -1;
    }

    time_t now = time(NULL);

    // Cleanup L1
    int l1Removed = SharedCache_CleanupExpired(cache->sharedCache, now);

    // Cleanup L2
    int l2Removed = SQLiteCache_CleanupExpired(&cache->sqliteCache);

    LOG_INFO("Cache cleanup: L1=%d, L2=%d entries removed", l1Removed, l2Removed);

    return l1Removed + l2Removed;
}

void HybridCache_GetStats(HybridCache *cache, CacheStats *statsOut) {
    if (!cache || !statsOut) {
        return;
    }

    statsOut->l1Hits = cache->l1Hits;
    statsOut->l2Hits = cache->l2Hits;
    statsOut->misses = cache->misses;
    statsOut->promotions = cache->promotions;

    unsigned long total = cache->l1Hits + cache->l2Hits + cache->misses;
    if (total > 0) {
        statsOut->l1HitRate = (double)cache->l1Hits / total * 100.0;
        statsOut->l2HitRate = (double)cache->l2Hits / total * 100.0;
        statsOut->totalHitRate = (double)(cache->l1Hits + cache->l2Hits) / total * 100.0;
    } else {
        statsOut->l1HitRate = 0.0;
        statsOut->l2HitRate = 0.0;
        statsOut->totalHitRate = 0.0;
    }
}

void HybridCache_Close(HybridCache *cache) {
    if (cache && cache->isInitialized) {
        SQLiteCache_Close(&cache->sqliteCache);
        // Note: We don't destroy shared memory here (other processes may use it)
        cache->isInitialized = false;
        LOG_INFO("Hybrid cache closed");
    }
}
```

---

## Testing

### Test 1: Unit test för SharedCache

**Fil:** `src/tests/unit/test_shared_cache.c`

```c
#include "../../infrastructure/cache/SharedCache.h"
#include "../../infrastructure/logging/Logger.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

void test_shared_cache_basic() {
    printf("Testing SharedCache basic operations...\n");

    SharedCacheData *cache = NULL;
    assert(SharedCache_Init(&cache) == 0);
    assert(cache != NULL);

    time_t now = time(NULL);
    time_t expiresAt = now + 900; // 15 min

    // Test SET
    const char *testData = "{\"temperature\": 15.5}";
    assert(SharedCache_Set(cache, "59.33,18.07", testData, expiresAt) == 0);

    // Test GET (HIT)
    char buffer[SHARED_CACHE_JSON_SIZE];
    assert(SharedCache_Get(cache, "59.33,18.07", buffer, now) == 0);
    assert(strcmp(buffer, testData) == 0);

    // Test GET (MISS - wrong key)
    assert(SharedCache_Get(cache, "60.00,19.00", buffer, now) == -1);

    // Test expiry
    time_t future = now + 1000;
    assert(SharedCache_Get(cache, "59.33,18.07", buffer, future) == -1);

    printf("✅ SharedCache basic tests passed\n");
}

int main() {
    Logger_Init(LOG_LEVEL_DEBUG, "logs/test_shared_cache.log");

    test_shared_cache_basic();

    printf("\n✅ All SharedCache tests passed!\n");
    return 0;
}
```

### Test 2: Integration test för HybridCache

**Fil:** `src/tests/unit/test_hybrid_cache.c`

```c
#include "../../infrastructure/cache/HybridCache.h"
#include "../../infrastructure/logging/Logger.h"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>

void test_hybrid_cache_flow() {
    printf("Testing HybridCache L1 → L2 → MISS flow...\n");

    HybridCache cache;
    assert(HybridCache_Init(&cache, "data/test_cache.db") == 0);

    const char *testWeather = "{\"temp\": 15.5, \"humidity\": 80}";
    double lat = 59.33;
    double lon = 18.07;

    // ═══ Test 1: MISS (both levels empty) ═══
    printf("Test 1: Cache MISS...\n");
    char *result = NULL;
    CacheLevel level;
    assert(HybridCache_GetWeather(&cache, lat, lon, &result, &level) == -1);
    assert(level == CACHE_LEVEL_NONE);

    // ═══ Test 2: SET (writes to both L1 and L2) ═══
    printf("Test 2: Cache SET...\n");
    assert(HybridCache_SetWeather(&cache, lat, lon, testWeather, 900) == 0);

    // ═══ Test 3: GET (should HIT L1) ═══
    printf("Test 3: Cache L1 HIT...\n");
    result = NULL;
    assert(HybridCache_GetWeather(&cache, lat, lon, &result, &level) == 0);
    assert(level == CACHE_LEVEL_SHARED_MEMORY);
    assert(strcmp(result, testWeather) == 0);
    free(result);

    // ═══ Test 4: Clear L1, check L2 promotion ═══
    printf("Test 4: L2 HIT and promotion to L1...\n");

    // Simulate L1 eviction (in real scenario, would be LRU or restart)
    SharedCache_CleanupExpired(cache.sharedCache, time(NULL) + 1000);

    result = NULL;
    assert(HybridCache_GetWeather(&cache, lat, lon, &result, &level) == 0);
    assert(level == CACHE_LEVEL_SQLITE); // L2 hit
    free(result);

    // Now should be in L1 again (promoted)
    result = NULL;
    assert(HybridCache_GetWeather(&cache, lat, lon, &result, &level) == 0);
    assert(level == CACHE_LEVEL_SHARED_MEMORY);
    free(result);

    // ═══ Test 5: Statistics ═══
    printf("Test 5: Cache statistics...\n");
    CacheStats stats;
    HybridCache_GetStats(&cache, &stats);
    printf("  L1 Hits: %lu\n", stats.l1Hits);
    printf("  L2 Hits: %lu\n", stats.l2Hits);
    printf("  Misses:  %lu\n", stats.misses);
    printf("  Promotions: %lu\n", stats.promotions);
    printf("  Total Hit Rate: %.2f%%\n", stats.totalHitRate);

    HybridCache_Close(&cache);

    printf("✅ HybridCache integration tests passed\n");
}

int main() {
    Logger_Init(LOG_LEVEL_DEBUG, "logs/test_hybrid_cache.log");

    test_hybrid_cache_flow();

    printf("\n✅ All HybridCache tests passed!\n");
    return 0;
}
```

---

## Integration med FetchWorker

### FetchWorker.c (Uppdaterad)

```c
// src/application/workers/FetchWorker.c
#include "FetchWorker.h"
#include "../../infrastructure/cache/HybridCache.h"
#include "../../infrastructure/logging/Logger.h"
#include "../services/Fetcher.h"

static HybridCache g_cache;

void FetchWorker_Init(void) {
    // Initialize hybrid cache
    if (HybridCache_Init(&g_cache, "data/gridguard.db") != 0) {
        LOG_FATAL("Failed to initialize hybrid cache");
        exit(1);
    }
    LOG_INFO("FetchWorker initialized with hybrid cache");
}

void* FetchWorker_Thread(void *arg) {
    while (running) {
        WorkRequest req;
        Queue_Dequeue(&requestQueue, &req);

        // ═══════════════════════════════════════════════════
        // 1. CHECK CACHE FIRST (L1 → L2)
        // ═══════════════════════════════════════════════════
        char *cachedWeather = NULL;
        CacheLevel level;

        bool weatherHit = (HybridCache_GetWeather(&g_cache,
            req.userConfig.latitude,
            req.userConfig.longitude,
            &cachedWeather,
            &level) == 0);

        FetchData fetchData = {0};

        if (weatherHit) {
            LOG_INFO("Weather cache HIT (level %d) for %.2f,%.2f",
                     level, req.userConfig.latitude, req.userConfig.longitude);

            strncpy(fetchData.weatherJson, cachedWeather, sizeof(fetchData.weatherJson) - 1);
            free(cachedWeather);
        } else {
            // ═══════════════════════════════════════════════════
            // 2. CACHE MISS - Fetch from API
            // ═══════════════════════════════════════════════════
            LOG_INFO("Weather cache MISS, fetching from API...");

            if (FetchWeatherData(&req.userConfig, fetchData.weatherJson) == 0) {
                // ═══════════════════════════════════════════════════
                // 3. SAVE TO CACHE (both L1 and L2)
                // ═══════════════════════════════════════════════════
                HybridCache_SetWeather(&g_cache,
                    req.userConfig.latitude,
                    req.userConfig.longitude,
                    fetchData.weatherJson,
                    900); // 15 minutes TTL

                LOG_INFO("Weather data cached for %.2f,%.2f",
                         req.userConfig.latitude, req.userConfig.longitude);
            }
        }

        // Same for prices...
        // (Similar logic for spot prices)

        // Forward to ParseWorker
        Queue_Enqueue(&fetchQueue, &fetchData);
    }

    return NULL;
}

void FetchWorker_Cleanup(void) {
    // Print cache statistics before shutdown
    CacheStats stats;
    HybridCache_GetStats(&g_cache, &stats);

    LOG_INFO("=== Cache Statistics ===");
    LOG_INFO("L1 Hits: %lu (%.2f%%)", stats.l1Hits, stats.l1HitRate);
    LOG_INFO("L2 Hits: %lu (%.2f%%)", stats.l2Hits, stats.l2HitRate);
    LOG_INFO("Misses:  %lu", stats.misses);
    LOG_INFO("Promotions: %lu", stats.promotions);
    LOG_INFO("Total Hit Rate: %.2f%%", stats.totalHitRate);

    HybridCache_Close(&g_cache);
}
```

---

## Makefile uppdatering

```makefile
# Add SQLite library
LDFLAGS = -pthread -lcurl -lsqlite3 -lrt

# Add new source files
CACHE_SRCS = src/infrastructure/cache/SharedCache.c \
             src/infrastructure/cache/SQLiteCache.c \
             src/infrastructure/cache/HybridCache.c

DB_SRCS = src/infrastructure/database/Database.c

# Include in server build
SERVER_SRCS = ... $(CACHE_SRCS) $(DB_SRCS) ...

# Test targets
test-shared-cache: bin/test_shared_cache
	./bin/test_shared_cache

test-hybrid-cache: bin/test_hybrid_cache
	./bin/test_hybrid_cache

bin/test_shared_cache: src/tests/unit/test_shared_cache.c $(CACHE_SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

bin/test_hybrid_cache: src/tests/unit/test_hybrid_cache.c $(CACHE_SRCS) $(DB_SRCS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
```

---

## Performance Benchmarking

### Benchmark kod

```c
// src/tests/integration/test_cache_benchmark.c
#include <time.h>
#include <stdio.h>

void benchmark_cache_levels(void) {
    HybridCache cache;
    HybridCache_Init(&cache, "data/benchmark.db");

    const char *testData = "{\"temp\": 15.5}";
    HybridCache_SetWeather(&cache, 59.33, 18.07, testData, 900);

    struct timespec start, end;
    char *result;
    CacheLevel level;

    // Benchmark L1 (Shared Memory)
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < 10000; i++) {
        HybridCache_GetWeather(&cache, 59.33, 18.07, &result, &level);
        free(result);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    long l1_ns = (end.tv_sec - start.tv_sec) * 1000000000L +
                 (end.tv_nsec - start.tv_nsec);
    printf("L1 (Shared Memory): %ld ns per request\n", l1_ns / 10000);

    // Clear L1, benchmark L2
    SharedCache_CleanupExpired(cache.sharedCache, time(NULL) + 1000);

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < 10000; i++) {
        HybridCache_GetWeather(&cache, 59.33, 18.07, &result, &level);
        free(result);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    long l2_ns = (end.tv_sec - start.tv_sec) * 1000000000L +
                 (end.tv_nsec - start.tv_nsec);
    printf("L2 (SQLite):        %ld ns per request\n", l2_ns / 10000);

    printf("Speedup (L2/L1):    %.2fx\n", (double)l2_ns / l1_ns);
}
```

**Förväntade resultat:**
```
L1 (Shared Memory): ~50-200 ns per request
L2 (SQLite):        ~50,000-100,000 ns per request
Speedup (L2/L1):    ~500-1000x
```

---

## Checklista

- [ ] **Fas 1:** Database wrapper (Database.h/c)
- [ ] **Fas 2:** SharedCache (L1)
- [ ] **Fas 3:** SQLiteCache (L2)
- [ ] **Fas 4:** HybridCache (kombination)
- [ ] **Fas 5:** Unit tests
- [ ] **Fas 6:** Integration med FetchWorker
- [ ] **Fas 7:** Ta bort gamla Cache.c och CacheWorker.c
- [ ] **Fas 8:** Uppdatera Makefile
- [ ] **Fas 9:** Performance benchmarking
- [ ] **Fas 10:** Dokumentation

---

## Kursmål Coverage

### Vecka 5: IPC Advanced

✅ **Kursmål 2:** Redogöra för interprocesskommunikation som pipes, sockets och delat minne
- Shared memory implementation (`shm_open`, `mmap`)
- Process-shared mutex (`pthread_mutexattr_setpshared`)

✅ **Kursmål 8:** Använda IPC-lösningar för processkommunikation
- SharedCache kan delas mellan watchdog-restarts
- Flera server-instanser kan dela cache

### Vecka 10-11: Profilering & Optimering

✅ **Kursmål 6, 10, 11:** Profilering och optimering
- Jämföra L1 vs L2 prestanda
- Benchmarking med `clock_gettime`
- Cache hit rate analytics

---

## Sammanfattning

**Vad ni har uppnått:**
1. ✅ Production-ready cache med två nivåer
2. ✅ Shared memory (ns-latency för hot data)
3. ✅ SQLite (persistent storage för cold data)
4. ✅ Automatisk promotion från L2 → L1
5. ✅ Fullständig testning och benchmarking
6. ✅ Clean, documented, professional kod

**Nästa steg:**
- Börja med Fas 1 (Database wrapper)
- Testa varje fas innan ni går vidare
- Kör benchmark när allt är klart
