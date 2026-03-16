#define _POSIX_C_SOURCE 200809L

#include "watchdog/RestartPolicy.h"
#include "sys/Logger.h"

#include <string.h>
#include <time.h>

int RestartPolicy_Initiate(RestartPolicy *rp, int max_restarts, int window_sec, int base_backoff_sec)
{
    if (!rp)
        return -1;

    memset(rp, 0, sizeof(RestartPolicy));
    rp->max_restarts     = max_restarts;
    rp->window_sec       = window_sec;
    rp->base_backoff_sec = base_backoff_sec;

    return 0;
}

void RestartPolicy_Shutdown(RestartPolicy *rp)
{
    // No resources to clean up for stack-allocated struct
    (void)rp;
}

int RestartPolicy_CanRestart(RestartPolicy *rp)
{
    time_t now = time(NULL);

    if (rp->count > 0 && difftime(now, rp->first_restart) > rp->window_sec)
    {
        LOG_INFO("Watchdog: Restart window expired, resetting counter");
        rp->count = 0;
    }

    return rp->count < rp->max_restarts;
}

void RestartPolicy_RecordRestart(RestartPolicy *rp)
{
    time_t now = time(NULL);

    if (rp->count == 0)
        rp->first_restart = now;

    if (rp->count < rp->max_restarts)
        rp->timestamps[rp->count] = now;

    rp->count++;
}

int RestartPolicy_GetBackoffDelay(const RestartPolicy *rp)
{
    int delay = rp->base_backoff_sec;
    for (int i = 0; i < rp->count - 1 && delay < 32; i++)
        delay *= 2;
    return delay;
}

int RestartPolicy_GetCount(const RestartPolicy *rp)
{
    return rp->count;
}

int RestartPolicy_GetMax(const RestartPolicy *rp)
{
    return rp->max_restarts;
}
