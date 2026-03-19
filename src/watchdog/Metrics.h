#ifndef _METRICS_H_
#define _METRICS_H_

#include <time.h>
#include <sys/types.h>

#define WATCHDOG_METRICS_SHM_NAME "/gridguard_watchdog_metrics"

// Shared memory structure for watchdog metrics
// Server reads this to expose status via API
typedef struct
{
    time_t watchdogStartTime;
    time_t lastRestartTime;
    int    restartCount;
    int    maxRestarts;
    int    restartWindowSec;

    pid_t  fetcherPid;
    time_t fetcherStartTime;
    time_t fetcherLastHeartbeat;

    pid_t  parserPid;
    time_t parserStartTime;
    time_t parserLastHeartbeat;

    pid_t  serverPid;
    time_t serverStartTime;
    time_t serverLastHeartbeat;
} WatchdogMetrics;

// Watchdog side - creates shared memory
int  Metrics_Initiate(void);
void Metrics_Shutdown(void);
WatchdogMetrics *Metrics_GetWritable(void);

// Server side - read-only access
WatchdogMetrics *Metrics_Open(void);
void Metrics_Close(WatchdogMetrics *metrics);

void Metrics_Update(WatchdogMetrics *metrics,
                    pid_t fetcherPid, time_t fetcherHb,
                    pid_t parserPid,  time_t parserHb,
                    pid_t serverPid,  time_t serverHb,
                    int restartCount, int maxRestarts);

#endif // _METRICS_H_
