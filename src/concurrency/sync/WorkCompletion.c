#define _POSIX_C_SOURCE 200809L

#include "WorkCompletion.h"
#include "Logger.h"
#include <string.h>
#include <time.h>

int WorkCompletion_Initiate(WorkCompletion *wc)
{
    if (!wc)
        return -1;

    memset(wc, 0, sizeof(WorkCompletion));
    pthread_mutex_init(&wc->mutex, NULL);

    // Use CLOCK_MONOTONIC so the timeout is immune to NTP adjustments
    // and manual system clock changes.
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&wc->cond, &attr);
    pthread_condattr_destroy(&attr);

    return 0;
}

void WorkCompletion_Destroy(WorkCompletion *wc)
{
    if (!wc)
        return;

    pthread_mutex_destroy(&wc->mutex);
    pthread_cond_destroy(&wc->cond);
}

void WorkCompletion_Signal(WorkCompletion *wc, const char *json)
{
    if (!wc || !json)
        return;

    pthread_mutex_lock(&wc->mutex);
    strncpy(wc->json, json, WORK_COMPLETION_BUFFER_SIZE - 1);
    wc->json[WORK_COMPLETION_BUFFER_SIZE - 1] = '\0';
    wc->done  = 1;
    wc->error = 0;
    pthread_cond_signal(&wc->cond);
    pthread_mutex_unlock(&wc->mutex);
}

void WorkCompletion_SignalError(WorkCompletion *wc)
{
    if (!wc)
        return;

    pthread_mutex_lock(&wc->mutex);
    wc->done  = 1;
    wc->error = 1;
    pthread_cond_signal(&wc->cond);
    pthread_mutex_unlock(&wc->mutex);
}

int WorkCompletion_Wait(WorkCompletion *wc)
{
    if (!wc)
        return -1;

    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += WORK_COMPLETION_TIMEOUT_SEC;

    pthread_mutex_lock(&wc->mutex);

    while (!wc->done)
    {
        int rc = pthread_cond_timedwait(&wc->cond, &wc->mutex, &deadline);
        if (rc != 0)
        {
            pthread_mutex_unlock(&wc->mutex);
            LOG_ERROR("WorkCompletion: Timed out after %d seconds", WORK_COMPLETION_TIMEOUT_SEC);
            return -1;
        }
    }

    int err = wc->error;
    pthread_mutex_unlock(&wc->mutex);

    return err ? -1 : 0;
}
