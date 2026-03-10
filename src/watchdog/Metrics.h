#ifndef METRICS_H
#define METRICS_H

#include <time.h>
#include <sys/types.h>

#define WATCHDOG_METRICS_SHM_NAME "/gridguard_watchdog_metrics"

// Shared memory structure for watchdog metrics
// Server reads this to expose status via API
typedef struct {
    time_t watchdog_start_time;
    time_t last_restart_time;
    int    restart_count;
    int    max_restarts;
    int    restart_window_sec;

    pid_t  fetcher_pid;
    time_t fetcher_start_time;
    time_t fetcher_last_heartbeat;

    pid_t  parser_pid;
    time_t parser_start_time;
    time_t parser_last_heartbeat;

    pid_t  server_pid;
    time_t server_start_time;
    time_t server_last_heartbeat;
} WatchdogMetrics;

// Watchdog side - creates shared memory
int  Metrics_Initiate(void);
void Metrics_Shutdown(void);
WatchdogMetrics *Metrics_GetWritable(void);

// Server side - read-only access
WatchdogMetrics *Metrics_Open(void);
void Metrics_Close(WatchdogMetrics *metrics);

void Metrics_Update(WatchdogMetrics *metrics, pid_t fetcher_pid, time_t fetcher_hb, pid_t parser_pid, time_t parser_hb, pid_t server_pid, time_t server_hb, int restart_count, int max_restarts);

#endif
