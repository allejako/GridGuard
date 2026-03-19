#ifndef _RUNTIME_CONFIG_H_
#define _RUNTIME_CONFIG_H_

#include "config/ConfigParser.h"

// Forward declare pthread_rwlock_t to avoid include issues
typedef struct pthread_rwlock_t_wrapper *pthread_rwlock_ptr_t;

// Thread-safe runtime configuration system
// Loads config from file and provides read/write-locked access

typedef struct
{
    ConfigParser parser;
    void        *lock;        // pthread_rwlock_t* (opaque to avoid header issues)
    int          initialized;
} RuntimeConfig;

// Global config instance (defined in RuntimeConfig.c)
extern RuntimeConfig *g_config;

// Initialize and load config from file
// If path is NULL or file doesn't exist, uses defaults from Config.h
int RuntimeConfig_Load(const char *configPath);

// Get config value with fallback chain:
// 1. Runtime config (gridguard.conf)
// 2. Environment variable (if envVar != NULL)
// 3. Default value (from Config.h)
const char *RuntimeConfig_Get(const char *key, const char *envVar, const char *defaultValue);

// Get integer value with fallback chain
int RuntimeConfig_GetInt(const char *key, const char *envVar, int defaultValue);

// Reload config from file (called on SIGHUP)
int RuntimeConfig_Reload(void);

// Free all resources
void RuntimeConfig_Shutdown(void);

#endif // _RUNTIME_CONFIG_H_
