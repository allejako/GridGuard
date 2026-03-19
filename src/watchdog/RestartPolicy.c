#define _POSIX_C_SOURCE 200809L

#include "watchdog/RestartPolicy.h"
#include "sys/Logger.h"

#include <string.h>
#include <time.h>

int RestartPolicy_Initiate(RestartPolicy *rp, int maxRestarts, int windowSec, int baseBackoffSec)
{
    if (!rp)
        return -1;

    memset(rp, 0, sizeof(RestartPolicy));
    rp->maxRestarts     = maxRestarts;
    rp->windowSec       = windowSec;
    rp->baseBackoffSec  = baseBackoffSec;

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

    if (rp->count > 0 && difftime(now, rp->firstRestart) > rp->windowSec)
    {
        LOG_INFO("Watchdog: Restart window expired, resetting counter");
        rp->count = 0;
    }

    return rp->count < rp->maxRestarts;
}

void RestartPolicy_RecordRestart(RestartPolicy *rp)
{
    time_t now = time(NULL);

    if (rp->count == 0)
        rp->firstRestart = now;

    if (rp->count < rp->maxRestarts)
        rp->timestamps[rp->count] = now;

    rp->count++;
}

int RestartPolicy_GetBackoffDelay(const RestartPolicy *rp)
{
    int delay = rp->baseBackoffSec;
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
    return rp->maxRestarts;
}
