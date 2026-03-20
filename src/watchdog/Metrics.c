#define _POSIX_C_SOURCE 200809L

#include "watchdog/Metrics.h"
#include "sys/Logger.h"

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

// File descriptor and pointer maintained by watchdog process
static int metricsFd = -1;
static WatchdogMetrics *metricsPtr = NULL;

// Initialize shared memory segment for watchdog metrics
// Called by watchdog on startup. Creates a new shared memory region
// accessible by server for the /metrics endpoint.
int Metrics_Initiate(void)
{
    // Remove any stale shared memory from previous runs
    shm_unlink(WATCHDOG_METRICS_SHM_NAME);

    metricsFd = shm_open(WATCHDOG_METRICS_SHM_NAME, O_CREAT | O_RDWR, 0644);
    if (metricsFd < 0)
    {
        LOG_ERROR("WatchdogMetrics: shm_open() failed");
        return -1;
    }

    if (ftruncate(metricsFd, sizeof(WatchdogMetrics)) < 0)
    {
        LOG_ERROR("WatchdogMetrics: ftruncate() failed");
        close(metricsFd);
        shm_unlink(WATCHDOG_METRICS_SHM_NAME);
        return -1;
    }

    metricsPtr = mmap(NULL, sizeof(WatchdogMetrics), PROT_READ | PROT_WRITE, MAP_SHARED, metricsFd, 0);
    if (metricsPtr == MAP_FAILED)
    {
        LOG_ERROR("WatchdogMetrics: mmap() failed");
        close(metricsFd);
        shm_unlink(WATCHDOG_METRICS_SHM_NAME);
        return -1;
    }

    // Zero out metrics and record startup time
    memset(metricsPtr, 0, sizeof(WatchdogMetrics));
    metricsPtr->watchdogStartTime = time(NULL);

    LOG_INFO("WatchdogMetrics: Shared memory created at %s", WATCHDOG_METRICS_SHM_NAME);
    return 0;
}

// Clean up shared memory on watchdog shutdown
void Metrics_Shutdown(void)
{
    if (metricsPtr && metricsPtr != MAP_FAILED)
    {
        munmap(metricsPtr, sizeof(WatchdogMetrics));
        metricsPtr = NULL;
    }

    if (metricsFd >= 0)
    {
        close(metricsFd);
        metricsFd = -1;
    }

    shm_unlink(WATCHDOG_METRICS_SHM_NAME);
}

// Get writable pointer for watchdog process
// Returns the internal pointer created by Metrics_Initiate()
WatchdogMetrics *Metrics_GetWritable(void)
{
    return metricsPtr;
}

// Open shared memory for reading (used by server /metrics endpoint)
// Returns pointer to metrics or NULL on failure. Caller must call
// Metrics_Close() when done.
WatchdogMetrics *Metrics_Open(void)
{
    int fd = shm_open(WATCHDOG_METRICS_SHM_NAME, O_RDONLY, 0);
    if (fd < 0)
        return NULL;

    WatchdogMetrics *ptr = mmap(NULL, sizeof(WatchdogMetrics), PROT_READ, MAP_SHARED, fd, 0);
    close(fd);

    if (ptr == MAP_FAILED)
        return NULL;

    return ptr;
}

// Close shared memory mapping opened by Metrics_Open()
void Metrics_Close(WatchdogMetrics *metrics)
{
    if (metrics && metrics != MAP_FAILED)
        munmap(metrics, sizeof(WatchdogMetrics));
}

// Update all process metrics from watchdog monitoring loop
// Called after heartbeat checks to keep metrics current for /metrics endpoint
void Metrics_Update(WatchdogMetrics *metrics,
                    pid_t fetcherPid, time_t fetcherHb,
                    pid_t parserPid,  time_t parserHb,
                    pid_t serverPid,  time_t serverHb,
                    int restartCount, int maxRestarts)
{
    if (!metrics)
        return;

    metrics->fetcherPid           = fetcherPid;
    metrics->fetcherLastHeartbeat = fetcherHb;

    metrics->parserPid            = parserPid;
    metrics->parserLastHeartbeat  = parserHb;

    metrics->serverPid            = serverPid;
    metrics->serverLastHeartbeat  = serverHb;

    metrics->restartCount         = restartCount;
    metrics->maxRestarts          = maxRestarts;
}
