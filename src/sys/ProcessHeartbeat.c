#define _POSIX_C_SOURCE 200809L

#include "sys/ProcessHeartbeat.h"
#include "sys/Logger.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int ProcessHeartbeat_Initiate(ProcessHeartbeat *heartbeat, int intervalSec)
{
    if (!heartbeat) return -1;

    heartbeat->fd = -1;
    heartbeat->lastSent = 0;
    heartbeat->interval = intervalSec;

    const char *fdEnv = getenv("GRIDGUARD_HEARTBEAT_FD");
    if (!fdEnv)
    {
        LOG_INFO("ProcessHeartbeat: No heartbeat fd set, skipping heartbeat");
        return 0;
    }

    heartbeat->fd = atoi(fdEnv);
    if (heartbeat->fd < 0)
    {
        LOG_WARNING("ProcessHeartbeat: Invalid heartbeat fd: %s", fdEnv);
        heartbeat->fd = -1;
        return -1;
    }

    LOG_INFO("ProcessHeartbeat: Initialized (fd=%d, interval=%ds)", heartbeat->fd, intervalSec);
    return 0;
}

void ProcessHeartbeat_Shutdown(ProcessHeartbeat *heartbeat)
{
    if (!heartbeat)
        return;

    if (heartbeat->fd >= 0)
    {
        close(heartbeat->fd);
        heartbeat->fd = -1;
    }
}

int ProcessHeartbeat_Send(ProcessHeartbeat *heartbeat)
{
    if (!heartbeat || heartbeat->fd < 0) return 0;

    time_t now = time(NULL);
    if (difftime(now, heartbeat->lastSent) < heartbeat->interval)
        return 0;

    const char *msg = "heartbeat\n";
    ssize_t written = write(heartbeat->fd, msg, strlen(msg));
    if (written < 0)
    {
        LOG_WARNING("ProcessHeartbeat: write() failed, pipe may be broken");
        heartbeat->fd = -1;
        return -1;
    }

    heartbeat->lastSent = now;
    return 0;
}
