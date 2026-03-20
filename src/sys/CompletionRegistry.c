#define _POSIX_C_SOURCE 200809L

#include "sys/CompletionRegistry.h"
#include "sys/Logger.h"
#include <string.h>
#include <stdbool.h>

#define MAX_COMPLETIONS 1024

typedef struct
{
    char userId[64];
    WorkCompletion *completion;
    bool active;
} CompletionEntry;

static CompletionEntry g_registry[MAX_COMPLETIONS];
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

void CompletionRegistry_Initiate(void)
{
    pthread_mutex_lock(&g_mutex);
    memset(g_registry, 0, sizeof(g_registry));
    pthread_mutex_unlock(&g_mutex);
}

void CompletionRegistry_Shutdown(void)
{
    pthread_mutex_lock(&g_mutex);
    memset(g_registry, 0, sizeof(g_registry));
    pthread_mutex_unlock(&g_mutex);
}

void CompletionRegistry_Register(const char *userId, WorkCompletion *completion)
{
    if (!userId || !completion)
        return;

    pthread_mutex_lock(&g_mutex);

    // Find first free slot
    for (int i = 0; i < MAX_COMPLETIONS; i++)
    {
        if (!g_registry[i].active)
        {
            strncpy(g_registry[i].userId, userId, sizeof(g_registry[i].userId) - 1);
            g_registry[i].userId[sizeof(g_registry[i].userId) - 1] = '\0';
            g_registry[i].completion = completion;
            g_registry[i].active = true;
            pthread_mutex_unlock(&g_mutex);
            LOG_DEBUG("CompletionRegistry: Registered %s", userId);
            return;
        }
    }

    pthread_mutex_unlock(&g_mutex);
    LOG_ERROR("CompletionRegistry: No free slots (MAX_COMPLETIONS=%d)", MAX_COMPLETIONS);
}

WorkCompletion *CompletionRegistry_Find(const char *userId)
{
    if (!userId)
        return NULL;

    pthread_mutex_lock(&g_mutex);

    for (int i = 0; i < MAX_COMPLETIONS; i++)
    {
        if (g_registry[i].active && strcmp(g_registry[i].userId, userId) == 0)
        {
            WorkCompletion *completion = g_registry[i].completion;
            pthread_mutex_unlock(&g_mutex);
            return completion;
        }
    }

    pthread_mutex_unlock(&g_mutex);
    return NULL;
}

void CompletionRegistry_Unregister(const char *userId)
{
    if (!userId)
        return;

    pthread_mutex_lock(&g_mutex);

    for (int i = 0; i < MAX_COMPLETIONS; i++)
    {
        if (g_registry[i].active && strcmp(g_registry[i].userId, userId) == 0)
        {
            g_registry[i].active = false;
            g_registry[i].userId[0] = '\0';
            g_registry[i].completion = NULL;
            pthread_mutex_unlock(&g_mutex);
            LOG_DEBUG("CompletionRegistry: Unregistered %s", userId);
            return;
        }
    }

    pthread_mutex_unlock(&g_mutex);
}
