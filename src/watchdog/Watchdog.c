#define _POSIX_C_SOURCE 200809L

#include "watchdog/Watchdog.h"
#include "watchdog/Heartbeat.h"
#include "watchdog/RestartPolicy.h"
#include "watchdog/Metrics.h"
#include "watchdog/ProcessSpawner.h"
#include "watchdog/IPCPaths.h"
#include "sys/Logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <stdarg.h>

#define STATUS_FIFO_PATH "/tmp/gridguard.status"
#define MONITOR_POLL_SEC  2

typedef struct {
    char *fifoPath;
    int fd;
} Status;

static Status *statusCreate(const char *fifoPath)
{
    if (!fifoPath)
        return NULL;

    Status *status = malloc(sizeof(Status));
    if (!status)
        return NULL;

    status->fifoPath = strdup(fifoPath);
    if (!status->fifoPath)
    {
        free(status);
        return NULL;
    }

    if (mkfifo(fifoPath, 0600) < 0 && errno != EEXIST)
    {
        LOG_WARNING("Status: mkfifo failed: %s", strerror(errno));
        free(status->fifoPath);
        free(status);
        return NULL;
    }

    status->fd = open(fifoPath, O_RDWR | O_NONBLOCK);
    if (status->fd < 0)
    {
        LOG_WARNING("Status: could not open status fifo: %s", strerror(errno));
        free(status->fifoPath);
        free(status);
        return NULL;
    }

    LOG_INFO("Status FIFO opened: %s", fifoPath);
    return status;
}

static void statusWrite(Status *status, const char *fmt, ...)
{
    if (!status || status->fd < 0)
        return;

    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n > 0)
        write(status->fd, buf, (size_t)n);
}

static void statusDestroy(Status *status)
{
    if (!status)
        return;

    if (status->fd >= 0)
    {
        close(status->fd);
        status->fd = -1;
    }

    if (status->fifoPath)
    {
        unlink(status->fifoPath);
        free(status->fifoPath);
    }

    free(status);
}

static int ipcCreateFifos(void)
{
    int errors = 0;

    if (mkfifo(REQUEST_FIFO_PATH, 0644) < 0 && errno != EEXIST)
    {
        LOG_WARNING("IPC: mkfifo(%s) failed: %s", REQUEST_FIFO_PATH, strerror(errno));
        errors++;
    }

    if (mkfifo(FETCH_TO_PARSE_FIFO_PATH, 0644) < 0 && errno != EEXIST)
    {
        LOG_WARNING("IPC: mkfifo(%s) failed: %s", FETCH_TO_PARSE_FIFO_PATH, strerror(errno));
        errors++;
    }

    unlink(PARSE_TO_COMPUTE_SOCK_PATH);

    return errors > 0 ? -1 : 0;
}

static void ipcCleanup(void)
{
    unlink(REQUEST_FIFO_PATH);
    unlink(FETCH_TO_PARSE_FIFO_PATH);
    unlink(PARSE_TO_COMPUTE_SOCK_PATH);
}

// Remove shared memory cache segments.
// A crashed process may have left an rwlock in a locked or inconsistent state
// inside the segment. Unlinking forces fresh initialization on next startup.
// Safe to call when no processes are using the segments.
static void shmCleanupCaches(void)
{
    shm_unlink("/gridguard_weather");
    shm_unlink("/gridguard_price");
    shm_unlink("/gridguard_forecast");
}

static volatile sig_atomic_t watchdogRunning = 1;
static volatile sig_atomic_t logProcessStatus = 0;
static volatile sig_atomic_t manualRestart = 0;
static volatile pid_t        fetcherPid = -1;
static volatile pid_t        parserPid = -1;
static volatile pid_t        serverPid = -1;

static void signalHandler(int signum)
{
    if (signum == SIGTERM || signum == SIGINT)
    {
        watchdogRunning = 0; 

        if (fetcherPid > 0)
            kill(fetcherPid, SIGTERM);
        if (parserPid > 0)
            kill(parserPid, SIGTERM);
        if (serverPid > 0)
            kill(serverPid, SIGTERM);
    }
    else if (signum == SIGHUP)
    {
        if (fetcherPid > 0)
            kill(fetcherPid, SIGHUP);
        if (parserPid > 0)
            kill(parserPid, SIGHUP);
        if (serverPid > 0)
            kill(serverPid, SIGHUP);
    }
    else if (signum == SIGUSR1)
    {
        logProcessStatus = 1;
    }
    else if (signum == SIGUSR2)
    {
        manualRestart = 1;
    }
}

static void signalsSetup(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGHUP,  &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);

    signal(SIGPIPE, SIG_IGN);
}

int Watchdog_Run(const char *fetcherPath, const char *parserPath, const char *serverPath)
{
    signalsSetup();

    Status *status = statusCreate(STATUS_FIFO_PATH);
    if (!status)
    {
        LOG_WARNING("Watchdog: Failed to create status FIFO, continuing without it");
    }

    if (ipcCreateFifos() != 0)
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
        metrics->restartWindowSec = RESTART_WINDOW_SEC;
    }

    RestartPolicy policy;
    if (RestartPolicy_Initiate(&policy, MAX_RESTARTS, RESTART_WINDOW_SEC, BASE_BACKOFF_SEC) != 0)
    {
        LOG_FATAL("Watchdog: Failed to initiate restart policy");
        ipcCleanup();
        statusDestroy(status);
        return 1;
    }

    ProcessGroup group;
    ProcessGroup_Initiate(&group, fetcherPath, parserPath, serverPath);

    // Clear any stale SHM segments from a previous crash before spawning.
    shmCleanupCaches();

    LOG_INFO("Starting all processes");

    if (ProcessGroup_SpawnAll(&group) != 0)
    {
        LOG_FATAL("Watchdog: Failed to spawn all processes");
        ProcessGroup_Cleanup(&group);
        RestartPolicy_Shutdown(&policy);
        ipcCleanup();
        statusDestroy(status);
        return 1;
    }

    fetcherPid = group.fetcher.pid;
    parserPid = group.parser.pid;
    serverPid = group.server.pid;

    statusWrite(status, "START fetcher=%d parser=%d server=%d\n", (int)fetcherPid, (int)parserPid, (int)serverPid);

    int waitStatus;
    time_t lastFetcherHb = time(NULL);
    time_t lastParserHb = time(NULL);
    time_t lastServerHb = time(NULL);
    time_t processStartTime = time(NULL);

    if (metrics)
    {
        metrics->fetcherPid       = fetcherPid;
        metrics->fetcherStartTime = processStartTime;
        metrics->parserPid        = parserPid;
        metrics->parserStartTime  = processStartTime;
        metrics->serverPid        = serverPid;
        metrics->serverStartTime  = processStartTime;
    }

    while (watchdogRunning)
    {
        if (logProcessStatus)
        {
            logProcessStatus = 0;
            time_t now = time(NULL);
            LOG_INFO("GRIDGUARD PROCESSES STATUS REPORT:");
            LOG_INFO("Fetcher: PID %d, Last heartbeat %.0fs ago", (int)fetcherPid, difftime(now, lastFetcherHb));
            LOG_INFO("Parser:  PID %d, Last heartbeat %.0fs ago", (int)parserPid, difftime(now, lastParserHb));
            LOG_INFO("Server:  PID %d, Last heartbeat %.0fs ago", (int)serverPid, difftime(now, lastServerHb));
            LOG_INFO("Restarts: %d/%d (window: %ds)", RestartPolicy_GetCount(&policy), RestartPolicy_GetMax(&policy), RESTART_WINDOW_SEC);
            LOG_INFO("///---///---//---///");
            statusWrite(status, "STATUS fetcher=%d parser=%d server=%d restarts=%d/%d\n", (int)fetcherPid, (int)parserPid, (int)serverPid, RestartPolicy_GetCount(&policy), RestartPolicy_GetMax(&policy));
        }

        if (manualRestart)
        {
            manualRestart = 0;
            LOG_INFO("Manual restart requested via SIGUSR2");
            statusWrite(status, "MANUAL_RESTART fetcher=%d parser=%d server=%d\n", (int)fetcherPid, (int)parserPid, (int)serverPid);

            ProcessGroup_KillAll(&group, SIGTERM);
            sleep(2);
            ProcessGroup_KillAll(&group, SIGKILL);
            ProcessGroup_WaitAll(&group);

            goto manualRestartAll;
        }

        int fetcherHbResult = Heartbeat_Check(&group.fetcher.heartbeat, MONITOR_POLL_SEC);
        if (fetcherHbResult == 1)
            lastFetcherHb = time(NULL);

        int parserHbResult = Heartbeat_Check(&group.parser.heartbeat, MONITOR_POLL_SEC);
        if (parserHbResult == 1)
            lastParserHb = time(NULL);

        int serverHbResult = Heartbeat_Check(&group.server.heartbeat, MONITOR_POLL_SEC);
        if (serverHbResult == 1)
            lastServerHb = time(NULL);

        if (metrics)
        {
            Metrics_Update(metrics, fetcherPid, lastFetcherHb, parserPid, lastParserHb, serverPid, lastServerHb, RestartPolicy_GetCount(&policy), RestartPolicy_GetMax(&policy));
        }

        pid_t frozenPid = -1;
        const char *frozenName = NULL;

        if (difftime(time(NULL), lastFetcherHb) >= HEARTBEAT_TIMEOUT)
        {
            frozenPid = fetcherPid;
            frozenName = "Fetcher";
        }
        else if (difftime(time(NULL), lastParserHb) >= HEARTBEAT_TIMEOUT)
        {
            frozenPid = parserPid;
            frozenName = "Parser";
        }
        else if (difftime(time(NULL), lastServerHb) >= HEARTBEAT_TIMEOUT)
        {
            frozenPid = serverPid;
            frozenName = "Server";
        }

        if (frozenPid > 0)
        {
            LOG_WARNING("Watchdog: %s (PID %d) frozen, killing all processes", frozenName, frozenPid);
            statusWrite(status, "FROZEN process=%s pid=%d\n", frozenName, frozenPid);

            ProcessGroup_KillAll(&group, SIGTERM);
            sleep(2);
            ProcessGroup_KillAll(&group, SIGKILL);
            ProcessGroup_WaitAll(&group);

            goto allDied;
        }

        // waitpid() with -1 waits for ANY child process.
        // WNOHANG returns immediately if no child has exited (non-blocking).
        // Returns: child PID if exited, 0 if still running, -1 on error.
        pid_t result = waitpid(-1, &waitStatus, WNOHANG);

        if (result < 0)
        {
            if (errno == EINTR)
                continue;
            if (errno == ECHILD)  // No more children
                break;
            LOG_ERROR("Watchdog: waitpid() failed: %s", strerror(errno));
            break;
        }

        if (result == 0)  // No child exited yet
            continue;

        allDied:
        manualRestartAll:

        if (!watchdogRunning)
        {
            LOG_INFO("Watchdog: Process died during shutdown, waiting for others...");
            continue;
        }

        const char *deadProcess = "Unknown";
        if (result == fetcherPid)
            deadProcess = "Fetcher";
        else if (result == parserPid)
            deadProcess = "Parser";
        else if (result == serverPid)
            deadProcess = "Server";

        // Check how child process terminated
        if (WIFEXITED(waitStatus))
        {
            // Process called exit() or returned from main()
            int code = WEXITSTATUS(waitStatus);
            LOG_WARNING("Watchdog: %s exited with code %d", deadProcess, code);
            statusWrite(status, "CRASH process=%s code=%d\n", deadProcess, code);
        }
        else if (WIFSIGNALED(waitStatus))
        {
            // Process was killed by signal (SIGTERM, SIGKILL, SIGSEGV, etc.)
            int sig = WTERMSIG(waitStatus);
            LOG_WARNING("Watchdog: %s killed by signal %d (%s)", deadProcess, sig, strsignal(sig));
            statusWrite(status, "CRASH process=%s signal=%d\n", deadProcess, sig);
        }

        LOG_INFO("Killing all processes due to %s crash", deadProcess);
        ProcessGroup_KillAll(&group, SIGTERM);
        sleep(1);
        waitpid(fetcherPid, NULL, WNOHANG);
        waitpid(parserPid, NULL, WNOHANG);
        waitpid(serverPid, NULL, WNOHANG);

        if (!watchdogRunning)
        {
            LOG_INFO("Watchdog: Shutdown requested during crash handling, exiting");
            break;
        }

        if (!RestartPolicy_CanRestart(&policy))
        {
            LOG_FATAL("Watchdog: Max restarts (%d) exceeded in %d seconds, giving up", RestartPolicy_GetMax(&policy), RESTART_WINDOW_SEC);
            statusWrite(status, "FATAL max_restarts=%d\n", RestartPolicy_GetMax(&policy));
            ProcessGroup_Cleanup(&group);
            RestartPolicy_Shutdown(&policy);
            ipcCleanup();
            statusDestroy(status);
            return 1;
        }

        RestartPolicy_RecordRestart(&policy);
        int backoff = RestartPolicy_GetBackoffDelay(&policy);

        LOG_INFO("Restarting all processes in %d seconds (attempt %d/%d)", backoff, RestartPolicy_GetCount(&policy), RestartPolicy_GetMax(&policy));

        for (int i = 0; i < backoff && watchdogRunning; i++)
            sleep(1);

        if (!watchdogRunning)
            break;

        ipcCleanup();
        ipcCreateFifos();
        shmCleanupCaches();

        if (ProcessGroup_SpawnAll(&group) != 0)
        {
            LOG_FATAL("Watchdog: Failed to respawn processes");
            ProcessGroup_Cleanup(&group);
            RestartPolicy_Shutdown(&policy);
            ipcCleanup();
            statusDestroy(status);
            return 1;
        }

        // Update pids after respawn
        fetcherPid = group.fetcher.pid;
        parserPid = group.parser.pid;
        serverPid = group.server.pid;

        statusWrite(status, "RESTART attempt=%d/%d fetcher=%d parser=%d server=%d delay=%ds\n", RestartPolicy_GetCount(&policy), RestartPolicy_GetMax(&policy), (int)fetcherPid, (int)parserPid, (int)serverPid, backoff);

        lastFetcherHb = time(NULL);
        lastParserHb = time(NULL);
        lastServerHb = time(NULL);
        time_t restartTime = time(NULL);

        if (metrics)
        {
            metrics->fetcherStartTime  = restartTime;
            metrics->parserStartTime   = restartTime;
            metrics->serverStartTime   = restartTime;
            metrics->lastRestartTime   = restartTime;
        }
    }

    LOG_INFO("Watchdog: Main loop exited, ensuring all processes are terminated");

    if (!watchdogRunning)
    {
        LOG_INFO("Watchdog: Waiting up to 3 seconds for graceful shutdown");
        for (int i = 0; i < 3; i++)
        {
            if (waitpid(fetcherPid, NULL, WNOHANG) == fetcherPid &&
                waitpid(parserPid, NULL, WNOHANG) == parserPid &&
                waitpid(serverPid, NULL, WNOHANG) == serverPid)
            {
                LOG_INFO("Watchdog: All processes exited gracefully");
                break;
            }
            sleep(1);
        }

        ProcessGroup_KillAll(&group, SIGKILL);
    }

    ProcessGroup_WaitAll(&group);

    ProcessGroup_Cleanup(&group);
    RestartPolicy_Shutdown(&policy);

    Metrics_Shutdown();

    ipcCleanup();
    shmCleanupCaches();
    statusDestroy(status);
    LOG_INFO("Watchdog exiting");
    return 0;
}
