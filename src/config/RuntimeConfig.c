#define _POSIX_C_SOURCE 200809L

#include "config/RuntimeConfig.h"
#include "sys/Logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <pthread.h>

// Global config instance
static RuntimeConfig g_runtime_config = {0};
RuntimeConfig *g_config = &g_runtime_config;

// Actual rwlock storage (hidden from header)
static pthread_rwlock_t g_rwlock;

// Config file path (set during load)
static char g_config_path[512] = "";

int RuntimeConfig_Load(const char *configPath)
{
    if (g_config->initialized)
    {
        LOG_WARNING("RuntimeConfig already initialized, call RuntimeConfig_Reload instead");
        return 0;
    }

    // Initialize parser
    if (ConfigParser_Initiate(&g_config->parser) != 0)
    {
        LOG_ERROR("RuntimeConfig: Failed to initialize parser");
        return -1;
    }

    // Initialize rwlock
    if (pthread_rwlock_init(&g_rwlock, NULL) != 0)
    {
        LOG_ERROR("RuntimeConfig: Failed to initialize rwlock");
        ConfigParser_Shutdown(&g_config->parser);
        return -1;
    }

    g_config->lock = &g_rwlock;
    g_config->initialized = 1;

    // Try to load config file if provided
    if (configPath)
    {
        strncpy(g_config_path, configPath, sizeof(g_config_path) - 1);

        if (ConfigParser_ParseFile(&g_config->parser, configPath) == 0)
        {
            LOG_INFO("RuntimeConfig: Loaded config from %s", configPath);
        }
        else
        {
            LOG_WARNING("RuntimeConfig: Failed to load %s, using defaults", configPath);
        }
    }
    else
    {
        LOG_INFO("RuntimeConfig: No config file provided, using defaults");
    }

    return 0;
}

const char *RuntimeConfig_Get(const char *key, const char *envVar, const char *defaultValue)
{
    if (!g_config->initialized)
    {
        // Not initialized yet, fallback to env or default
        if (envVar)
        {
            const char *envValue = getenv(envVar);
            if (envValue) return envValue;
        }
        return defaultValue;
    }

    pthread_rwlock_rdlock(&g_rwlock);

    // 1. Try runtime config
    const char *value = ConfigParser_Get(&g_config->parser, key);

    pthread_rwlock_unlock(&g_rwlock);

    if (value) return value;

    // 2. Try environment variable
    if (envVar)
    {
        const char *envValue = getenv(envVar);
        if (envValue) return envValue;
    }

    // 3. Use default
    return defaultValue;
}

int RuntimeConfig_GetInt(const char *key, const char *envVar, int defaultValue)
{
    const char *strValue = RuntimeConfig_Get(key, envVar, NULL);

    if (!strValue)
        return defaultValue;

    char *endptr;
    long result = strtol(strValue, &endptr, 10);

    if (*endptr != '\0' || result < INT_MIN || result > INT_MAX)
    {
        LOG_WARNING("RuntimeConfig: Invalid integer value for %s: %s", key, strValue);
        return defaultValue;
    }

    return (int)result;
}

int RuntimeConfig_Reload(void)
{
    if (!g_config->initialized)
    {
        LOG_ERROR("RuntimeConfig: Not initialized, cannot reload");
        return -1;
    }

    if (g_config_path[0] == '\0')
    {
        LOG_INFO("RuntimeConfig: No config file path set, nothing to reload");
        return 0;
    }

    // Create new parser
    ConfigParser newParser;
    if (ConfigParser_Initiate(&newParser) != 0)
    {
        LOG_ERROR("RuntimeConfig: Failed to initialize new parser for reload");
        return -1;
    }

    // Parse config file
    if (ConfigParser_ParseFile(&newParser, g_config_path) != 0)
    {
        LOG_ERROR("RuntimeConfig: Failed to reload config from %s", g_config_path);
        ConfigParser_Shutdown(&newParser);
        return -1;
    }

    // Swap parsers with write lock
    pthread_rwlock_wrlock(&g_rwlock);

    ConfigParser oldParser = g_config->parser;
    g_config->parser = newParser;

    pthread_rwlock_unlock(&g_rwlock);

    // Free old parser
    ConfigParser_Shutdown(&oldParser);

    LOG_INFO("RuntimeConfig: Reloaded config from %s", g_config_path);
    return 0;
}

void RuntimeConfig_Shutdown(void)
{
    if (!g_config->initialized)
        return;

    pthread_rwlock_wrlock(&g_rwlock);

    ConfigParser_Shutdown(&g_config->parser);
    g_config->initialized = 0;
    g_config->lock = NULL;

    pthread_rwlock_unlock(&g_rwlock);

    pthread_rwlock_destroy(&g_rwlock);

    LOG_INFO("RuntimeConfig: Shutdown complete");
}
