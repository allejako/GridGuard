#ifndef CACHE_H
#define CACHE_H

#include <stdbool.h>
#include <time.h>

#define MAX_CACHE_ENTRIES 100
#define MAX_KEY_LENGTH 64
#define MAX_DATA_LENGTH 4096
#define DEFAULT_TTL_SECONDS 900

typedef struct {
    char key[MAX_KEY_LENGTH];
    char data[MAX_DATA_LENGTH];
    time_t timestamp;
    int ttlSeconds;
    bool isValid;
} CacheEntry;

int cacheInit(void);
int cacheSet(const char *key, const char *data, int ttlSeconds);
const char* cacheGet(const char *key);
bool cacheIsValid(const char *key);
int cacheInvalidate(const char *key);
void cacheClear(void);
void cachePrintStats(void);
void cacheDestroy(void);

#endif
