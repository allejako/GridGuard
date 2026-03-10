#define _POSIX_C_SOURCE 200809L

#include "watchdog/Watchdog.h"
#include "watchdog/Signals.h"
#include "watchdog/Heartbeat.h"
#include "watchdog/RestartPolicy.h"
#include "watchdog/Metrics.h"
#include "watchdog/IPC.h"
#include "watchdog/Status.h"
#include "watchdog/ProcessSpawner.h"
#include "sys/Logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#define MONITOR_POLL_SEC  2
#define STATUS_FIFO_PATH  "/tmp/gridguard.status"

// Global state shared with signal handler
volatile sig_atomic_t watchdog_running = 1;
volatile pid_t        fetcher_pid      = -1;
volatile pid_t        parser_pid       = -1;
volatile pid_t        server_pid       = -1;

int Watchdog_Run(const char *fetcherPath, const char *parserPath, const char *serverPath)
{
    Signals_Initiate();

    Status *status = Status_Initiate(STATUS_FIFO_PATH);
    if (!status)
    {
        LOG_WARNING("Watchdog: Failed to create status FIFO, continuing without it");
    }

    if (IPC_Initiate() != 0)
    {
        LOG_WARNING("Watchdog: Some FIFOs failed to create");
    }

    if (Metrics_Initiate() != 0)
    {
        LOG_WARNING("Watchdog: Failed to create metrics shared memory, continuing without metrics");
    }

    WatchdogMetrics *metrics = Metrics_GetWritable();
    if (metrics)
    {
        metrics->restart_window_sec = RESTART_WINDOW_SEC;
    }

    RestartPolicy *policy = RestartPolicy_Create(MAX_RESTARTS, RESTART_WINDOW_SEC, BASE_BACKOFF_SEC);
    if (!policy)
    {
        LOG_FATAL("Watchdog: Failed to create restart policy");
        IPC_Shutdown();
        Status_Shutdown(status);
        return 1;
    }

    ProcessGroup group;
    ProcessGroup_Init(&group, fetcherPath, parserPath, serverPath);

    LOG_INFO("Starting all processes");

    if (ProcessGroup_SpawnAll(&group) != 0)
    {
        LOG_FATAL("Watchdog: Failed to spawn all processes");
        ProcessGroup_Cleanup(&group);
        RestartPolicy_Destroy(policy);
        IPC_Shutdown();
        Status_Shutdown(status);
        return 1;
    }

    // Update global PIDs for signal handler
    fetcher_pid = group.fetcher.pid;
    parser_pid = group.parser.pid;
    server_pid = group.server.pid;

    Status_Write(status, "START fetcher=%d parser=%d server=%d\n", (int)fetcher_pid, (int)parser_pid, (int)server_pid);

    int wait_status;
    time_t lastFetcherHb = time(NULL);
    time_t lastParserHb = time(NULL);
    time_t lastServerHb = time(NULL);
    time_t process_start_time = time(NULL);

    if (metrics)
    {
        metrics->fetcher_pid = fetcher_pid;
        metrics->fetcher_start_time = process_start_time;
        metrics->parser_pid = parser_pid;
        metrics->parser_start_time = process_start_time;
        metrics->server_pid = server_pid;
        metrics->server_start_time = process_start_time;
    }

    while (watchdog_running)
    {
        // === Signal-based Commands ===

        // SIGUSR1: Report current status without disrupting processes
        if (log_process_status)
        {
            log_process_status = 0;
            time_t now = time(NULL);
            LOG_INFO("GRIDGUARD PROCESSES STATUS REPORT:");
            LOG_INFO("Fetcher: PID %d, Last heartbeat %.0fs ago", (int)fetcher_pid, difftime(now, lastFetcherHb));
            LOG_INFO("Parser:  PID %d, Last heartbeat %.0fs ago", (int)parser_pid, difftime(now, lastParserHb));
            LOG_INFO("Server:  PID %d, Last heartbeat %.0fs ago", (int)server_pid, difftime(now, lastServerHb));
            LOG_INFO("Restarts: %d/%d (window: %ds)", RestartPolicy_GetCount(policy), RestartPolicy_GetMax(policy), RESTART_WINDOW_SEC);
            LOG_INFO("///---///---//---///");
            Status_Write(status, "STATUS fetcher=%d parser=%d server=%d restarts=%d/%d\n", (int)fetcher_pid, (int)parser_pid, (int)server_pid, RestartPolicy_GetCount(policy), RestartPolicy_GetMax(policy));
        }

        // SIGUSR2: Force immediate restart of all processes
        if (manual_restart)
        {
            manual_restart = 0;
            LOG_INFO("Manual restart requested via SIGUSR2");
            Status_Write(status, "MANUAL_RESTART fetcher=%d parser=%d server=%d\n", (int)fetcher_pid, (int)parser_pid, (int)server_pid);

            ProcessGroup_KillAll(&group, SIGTERM);
            sleep(2);
            ProcessGroup_KillAll(&group, SIGKILL);
            ProcessGroup_WaitAll(&group);

            goto manual_restart_all;
        }

        // === Heartbeat Monitoring ===

        // Check if any process has sent a heartbeat in this poll interval
        int fetcherHbResult = Heartbeat_Check(group.fetcher.heartbeat, MONITOR_POLL_SEC);
        if (fetcherHbResult == 1)
            lastFetcherHb = time(NULL);

        int parserHbResult = Heartbeat_Check(group.parser.heartbeat, MONITOR_POLL_SEC);
        if (parserHbResult == 1)
            lastParserHb = time(NULL);

        int serverHbResult = Heartbeat_Check(group.server.heartbeat, MONITOR_POLL_SEC);
        if (serverHbResult == 1)
            lastServerHb = time(NULL);

        // Update metrics
        if (metrics)
        {
            Metrics_Update(metrics, fetcher_pid, lastFetcherHb, parser_pid, lastParserHb, server_pid, lastServerHb, RestartPolicy_GetCount(policy), RestartPolicy_GetMax(policy));
        }

        // === Freeze Detection ===

        // Identify processes that haven't sent heartbeat within timeout window
        pid_t frozenPid = -1;
        const char *frozenName = NULL;

        if (difftime(time(NULL), lastFetcherHb) >= HEARTBEAT_TIMEOUT)
        {
            frozenPid = fetcher_pid;
            frozenName = "Fetcher";
        }
        else if (difftime(time(NULL), lastParserHb) >= HEARTBEAT_TIMEOUT)
        {
            frozenPid = parser_pid;
            frozenName = "Parser";
        }
        else if (difftime(time(NULL), lastServerHb) >= HEARTBEAT_TIMEOUT)
        {
            frozenPid = server_pid;
            frozenName = "Server";
        }

        if (frozenPid > 0)
        {
            LOG_WARNING("Watchdog: %s (PID %d) frozen, killing all processes", frozenName, frozenPid);
            Status_Write(status, "FROZEN process=%s pid=%d\n", frozenName, frozenPid);

            ProcessGroup_KillAll(&group, SIGTERM);
            sleep(2);
            ProcessGroup_KillAll(&group, SIGKILL);
            ProcessGroup_WaitAll(&group);

            goto all_died;
        }

        // === Crash Detection ===

        // Non-blocking check for terminated child processes
        pid_t result = waitpid(-1, &wait_status, WNOHANG);

        if (result < 0)
        {
            if (errno == EINTR)
                continue;
            if (errno == ECHILD)
                break;
            LOG_ERROR("Watchdog: waitpid() failed: %s", strerror(errno));
            break;
        }

        if (result == 0)
            continue;

        all_died:
        manual_restart_all:

        // === Failure Handling and Recovery ===

        // Identify which process triggered the restart
        const char *deadProcess = "Unknown";
        if (result == fetcher_pid)
            deadProcess = "Fetcher";
        else if (result == parser_pid)
            deadProcess = "Parser";
        else if (result == server_pid)
            deadProcess = "Server";

        if (WIFEXITED(wait_status))
        {
            int code = WEXITSTATUS(wait_status);
            if (code == 0 && !watchdog_running)
            {
                LOG_INFO("%s exited cleanly (exit 0)", deadProcess);
                Status_Write(status, "STOP process=%s exit=0\n", deadProcess);
                break;
            }
            LOG_WARNING("Watchdog: %s exited with code %d", deadProcess, code);
            Status_Write(status, "CRASH process=%s code=%d\n", deadProcess, code);
        }
        else if (WIFSIGNALED(wait_status))
        {
            int sig = WTERMSIG(wait_status);
            if (!watchdog_running)
            {
                LOG_INFO("Watchdog: %s terminated by signal %d during shutdown", deadProcess, sig);
                Status_Write(status, "STOP process=%s signal=%d\n", deadProcess, sig);
                break;
            }
            LOG_WARNING("Watchdog: %s killed by signal %d (%s)", deadProcess, sig, strsignal(sig));
            Status_Write(status, "CRASH process=%s signal=%d\n", deadProcess, sig);
        }

        // Terminate all processes for clean slate (one failure affects whole pipeline)
        LOG_INFO("Killing all processes due to %s crash", deadProcess);
        ProcessGroup_KillAll(&group, SIGTERM);
        sleep(1);
        waitpid(fetcher_pid, NULL, WNOHANG);
        waitpid(parser_pid, NULL, WNOHANG);
        waitpid(server_pid, NULL, WNOHANG);

        if (!watchdog_running)
            break;

        // Check restart policy (exponential backoff + rate limiting)
        if (!RestartPolicy_CanRestart(policy))
        {
            LOG_FATAL("Watchdog: Max restarts (%d) exceeded in %d seconds, giving up", RestartPolicy_GetMax(policy), RESTART_WINDOW_SEC);
            Status_Write(status, "FATAL max_restarts=%d\n", RestartPolicy_GetMax(policy));
            ProcessGroup_Cleanup(&group);
            RestartPolicy_Destroy(policy);
            IPC_Shutdown();
            Status_Shutdown(status);
            return 1;
        }

        RestartPolicy_RecordRestart(policy);
        int backoff = RestartPolicy_GetBackoffDelay(policy);

        LOG_INFO("Restarting all processes in %d seconds (attempt %d/%d)",
                 backoff, RestartPolicy_GetCount(policy), RestartPolicy_GetMax(policy));

        for (int i = 0; i < backoff && watchdog_running; i++)
            sleep(1);

        if (!watchdog_running)
            break;

        IPC_Shutdown();
        IPC_Initiate();

        if (ProcessGroup_SpawnAll(&group) != 0)
        {
            LOG_FATAL("Watchdog: Failed to respawn processes");
            ProcessGroup_Cleanup(&group);
            RestartPolicy_Destroy(policy);
            IPC_Shutdown();
            Status_Shutdown(status);
            return 1;
        }

        // Update global PIDs for signal handler
        fetcher_pid = group.fetcher.pid;
        parser_pid = group.parser.pid;
        server_pid = group.server.pid;

        Status_Write(status, "RESTART attempt=%d/%d fetcher=%d parser=%d server=%d delay=%ds\n", RestartPolicy_GetCount(policy), RestartPolicy_GetMax(policy), (int)fetcher_pid, (int)parser_pid, (int)server_pid, backoff);

        lastFetcherHb = time(NULL);
        lastParserHb = time(NULL);
        lastServerHb = time(NULL);
        time_t restart_time = time(NULL);

        if (metrics)
        {
            metrics->fetcher_start_time = restart_time;
            metrics->parser_start_time = restart_time;
            metrics->server_start_time = restart_time;
            metrics->last_restart_time = restart_time;
        }
    }

    LOG_INFO("Waiting for all processes to exit");
    ProcessGroup_WaitAll(&group);

    ProcessGroup_Cleanup(&group);
    RestartPolicy_Destroy(policy);

    Metrics_Shutdown();

    IPC_Shutdown();
    Status_Shutdown(status);
    LOG_INFO("Watchdog exiting");
    return 0;
}
