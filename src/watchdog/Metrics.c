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
static int metrics_fd = -1;
static WatchdogMetrics *metrics_ptr = NULL;

// Initialize shared memory segment for watchdog metrics
// Called by watchdog on startup. Creates a new shared memory region
// accessible by server for the /metrics endpoint.
int Metrics_Initiate(void)
{
    // Remove any stale shared memory from previous runs
    shm_unlink(WATCHDOG_METRICS_SHM_NAME);

    metrics_fd = shm_open(WATCHDOG_METRICS_SHM_NAME, O_CREAT | O_RDWR, 0644);
    if (metrics_fd < 0)
    {
        LOG_ERROR("WatchdogMetrics: shm_open() failed");
        return -1;
    }

    if (ftruncate(metrics_fd, sizeof(WatchdogMetrics)) < 0)
    {
        LOG_ERROR("WatchdogMetrics: ftruncate() failed");
        close(metrics_fd);
        shm_unlink(WATCHDOG_METRICS_SHM_NAME);
        return -1;
    }

    metrics_ptr = mmap(NULL, sizeof(WatchdogMetrics), PROT_READ | PROT_WRITE, MAP_SHARED, metrics_fd, 0);
    if (metrics_ptr == MAP_FAILED)
    {
        LOG_ERROR("WatchdogMetrics: mmap() failed");
        close(metrics_fd);
        shm_unlink(WATCHDOG_METRICS_SHM_NAME);
        return -1;
    }

    // Zero out metrics and record startup time
    memset(metrics_ptr, 0, sizeof(WatchdogMetrics));
    metrics_ptr->watchdogStartTime = time(NULL);

    LOG_INFO("WatchdogMetrics: Shared memory created at %s", WATCHDOG_METRICS_SHM_NAME);
    return 0;
}

// Clean up shared memory on watchdog shutdown
void Metrics_Shutdown(void)
{
    if (metrics_ptr && metrics_ptr != MAP_FAILED)
    {
        munmap(metrics_ptr, sizeof(WatchdogMetrics));
        metrics_ptr = NULL;
    }

    if (metrics_fd >= 0)
    {
        close(metrics_fd);
        metrics_fd = -1;
    }

    shm_unlink(WATCHDOG_METRICS_SHM_NAME);
}

// Get writable pointer for watchdog process
// Returns the internal pointer created by Metrics_Initiate()
WatchdogMetrics *Metrics_GetWritable(void)
{
    return metrics_ptr;
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
