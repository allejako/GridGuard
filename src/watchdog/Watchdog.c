#define _POSIX_C_SOURCE 200809L

#include "watchdog/Watchdog.h"
#include "watchdog/WatchdogSignals.h"
#include "watchdog/Heartbeat.h"
#include "watchdog/RestartPolicy.h"
#include "sys/Logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#define MONITOR_POLL_SEC           2
#define STATUS_FIFO_PATH           "/tmp/gridguard.status"
#define REQUEST_FIFO_PATH          "/tmp/gridguard_requests.fifo"
#define FETCH_TO_PARSE_FIFO_PATH   "/tmp/gridguard_fetch_to_parse.fifo"
#define PARSE_TO_COMPUTE_SOCK_PATH "/tmp/gridguard_parse_to_compute.sock"

volatile sig_atomic_t watchdog_running = 1;
volatile pid_t        fetcher_pid      = -1;
volatile pid_t        parser_pid       = -1;
volatile pid_t        server_pid       = -1;

static int status_fd = -1;

// ============================================================
// Status FIFO — named pipe for external readers (e.g. dashboard)
// ============================================================

static void status_open(void)
{
    if (mkfifo(STATUS_FIFO_PATH, 0600) < 0 && errno != EEXIST)
    {
        LOG_WARNING("Watchdog: mkfifo failed: %s", strerror(errno));
        return;
    }

    // O_RDWR keeps the write end open even without a reader on the other side
    status_fd = open(STATUS_FIFO_PATH, O_RDWR | O_NONBLOCK);
    if (status_fd < 0)
    {
        LOG_WARNING("Watchdog: could not open status fifo: %s", strerror(errno));
        return;
    }

    LOG_INFO("Status FIFO opened: %s", STATUS_FIFO_PATH);
}

static void status_write(const char *fmt, ...)
{
    if (status_fd < 0)
        return;

    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n > 0)
        write(status_fd, buf, (size_t)n);
}

static void status_close(void)
{
    if (status_fd >= 0)
    {
        close(status_fd);
        status_fd = -1;
    }
    unlink(STATUS_FIFO_PATH);
}

// ============================================================
// FIFO creation
// ============================================================

static void CreateFifos(void)
{
    if (mkfifo(REQUEST_FIFO_PATH, 0644) < 0 && errno != EEXIST)
        LOG_WARNING("Watchdog: mkfifo(%s) failed: %s", REQUEST_FIFO_PATH, strerror(errno));

    if (mkfifo(FETCH_TO_PARSE_FIFO_PATH, 0644) < 0 && errno != EEXIST)
        LOG_WARNING("Watchdog: mkfifo(%s) failed: %s", FETCH_TO_PARSE_FIFO_PATH, strerror(errno));

    unlink(PARSE_TO_COMPUTE_SOCK_PATH);
}

static void CleanupFifos(void)
{
    unlink(REQUEST_FIFO_PATH);
    unlink(FETCH_TO_PARSE_FIFO_PATH);
    unlink(PARSE_TO_COMPUTE_SOCK_PATH);
}

// ============================================================
// Process spawning
// ============================================================

static pid_t Watchdog_SpawnFetcher(const char *fetcherPath, Heartbeat **hbOut)
{
    Heartbeat_Destroy(*hbOut);
    *hbOut = Heartbeat_Create();
    if (!*hbOut)
        LOG_WARNING("Watchdog: Continuing without fetcher heartbeat pipe");

    pid_t pid = fork();

    if (pid < 0)
    {
        LOG_ERROR("Watchdog: fork() failed for fetcher: %s", strerror(errno));
        return -1;
    }

    if (pid == 0)
    {
        if (*hbOut)
        {
            Heartbeat_CloseReadFd(*hbOut);
            int writeFd = Heartbeat_GetWriteFd(*hbOut);
            char fdStr[16];
            snprintf(fdStr, sizeof(fdStr), "%d", writeFd);
            setenv("GRIDGUARD_HEARTBEAT_FD", fdStr, 1);
        }

        execl(fetcherPath, "GridGuard-fetcher", REQUEST_FIFO_PATH, FETCH_TO_PARSE_FIFO_PATH, NULL);

        fprintf(stderr, "Watchdog: execl(%s) failed: %s\n", fetcherPath, strerror(errno));
        _exit(127);
    }

    if (*hbOut)
        Heartbeat_CloseWriteFd(*hbOut);

    return pid;
}

static pid_t Watchdog_SpawnParser(const char *parserPath, Heartbeat **hbOut)
{
    Heartbeat_Destroy(*hbOut);
    *hbOut = Heartbeat_Create();
    if (!*hbOut)
        LOG_WARNING("Watchdog: Continuing without parser heartbeat pipe");

    pid_t pid = fork();

    if (pid < 0)
    {
        LOG_ERROR("Watchdog: fork() failed for parser: %s", strerror(errno));
        return -1;
    }

    if (pid == 0)
    {
        if (*hbOut)
        {
            Heartbeat_CloseReadFd(*hbOut);
            int writeFd = Heartbeat_GetWriteFd(*hbOut);
            char fdStr[16];
            snprintf(fdStr, sizeof(fdStr), "%d", writeFd);
            setenv("GRIDGUARD_HEARTBEAT_FD", fdStr, 1);
        }

        execl(parserPath, "GridGuard-parser", FETCH_TO_PARSE_FIFO_PATH, PARSE_TO_COMPUTE_SOCK_PATH, NULL);

        fprintf(stderr, "Watchdog: execl(%s) failed: %s\n", parserPath, strerror(errno));
        _exit(127);
    }

    if (*hbOut)
        Heartbeat_CloseWriteFd(*hbOut);

    return pid;
}

static pid_t Watchdog_SpawnServer(const char *serverPath, Heartbeat **hbOut)
{
    Heartbeat_Destroy(*hbOut);
    *hbOut = Heartbeat_Create();
    if (!*hbOut)
        LOG_WARNING("Watchdog: Continuing without server heartbeat pipe");

    pid_t pid = fork();

    if (pid < 0)
    {
        LOG_ERROR("Watchdog: fork() failed for server: %s", strerror(errno));
        return -1;
    }

    if (pid == 0)
    {
        if (*hbOut)
        {
            Heartbeat_CloseReadFd(*hbOut);
            int writeFd = Heartbeat_GetWriteFd(*hbOut);
            char fdStr[16];
            snprintf(fdStr, sizeof(fdStr), "%d", writeFd);
            setenv("GRIDGUARD_HEARTBEAT_FD", fdStr, 1);
        }

        execl(serverPath, "GridGuard-server", NULL);

        fprintf(stderr, "Watchdog: execl(%s) failed: %s\n", serverPath, strerror(errno));
        _exit(127);
    }

    if (*hbOut)
        Heartbeat_CloseWriteFd(*hbOut);

    return pid;
}

static void Watchdog_SpawnAll(const char *fetcherPath, const char *parserPath, const char *serverPath,
                               Heartbeat **fetcherHb, Heartbeat **parserHb, Heartbeat **serverHb,
                               pid_t *fetcherPid, pid_t *parserPid, pid_t *serverPid)
{
    // Start Parser first so it opens read end of FIFO before Fetcher tries to open write end
    *parserPid = Watchdog_SpawnParser(parserPath, parserHb);
    if (*parserPid < 0)
    {
        LOG_ERROR("Watchdog: Failed to spawn parser");
        return;
    }
    LOG_INFO("Parser started (PID %d)", *parserPid);

    sleep(1);

    *fetcherPid = Watchdog_SpawnFetcher(fetcherPath, fetcherHb);
    if (*fetcherPid < 0)
    {
        LOG_ERROR("Watchdog: Failed to spawn fetcher");
        kill(*parserPid, SIGTERM);
        return;
    }
    LOG_INFO("Fetcher started (PID %d)", *fetcherPid);

    sleep(1);

    *serverPid = Watchdog_SpawnServer(serverPath, serverHb);
    if (*serverPid < 0)
    {
        LOG_ERROR("Watchdog: Failed to spawn server");
        kill(*fetcherPid, SIGTERM);
        kill(*parserPid, SIGTERM);
        return;
    }
    LOG_INFO("Server started (PID %d)", *serverPid);
}

// ============================================================
// Main watchdog loop
// ============================================================

int Watchdog_Run(const char *fetcherPath, const char *parserPath, const char *serverPath)
{
    WatchdogSignals_Setup();
    status_open();
    CreateFifos();

    RestartPolicy *policy = RestartPolicy_Create(MAX_RESTARTS, RESTART_WINDOW_SEC,
                                                 BASE_BACKOFF_SEC);
    if (!policy)
    {
        LOG_FATAL("Watchdog: Failed to create restart policy");
        CleanupFifos();
        status_close();
        return 1;
    }

    Heartbeat *fetcherHb = NULL;
    Heartbeat *parserHb = NULL;
    Heartbeat *serverHb = NULL;

    LOG_INFO("Starting all processes");

    Watchdog_SpawnAll(fetcherPath, parserPath, serverPath,
                      &fetcherHb, &parserHb, &serverHb,
                      (pid_t*)&fetcher_pid, (pid_t*)&parser_pid, (pid_t*)&server_pid);

    if (fetcher_pid < 0 || parser_pid < 0 || server_pid < 0)
    {
        LOG_FATAL("Watchdog: Failed to spawn all processes");
        Heartbeat_Destroy(fetcherHb);
        Heartbeat_Destroy(parserHb);
        Heartbeat_Destroy(serverHb);
        RestartPolicy_Destroy(policy);
        CleanupFifos();
        status_close();
        return 1;
    }

    status_write("START fetcher=%d parser=%d server=%d\n", (int)fetcher_pid, (int)parser_pid, (int)server_pid);

    int status;
    time_t lastFetcherHb = time(NULL);
    time_t lastParserHb = time(NULL);
    time_t lastServerHb = time(NULL);

    while (watchdog_running)
    {
        // Check heartbeats for all three processes
        int fetcherHbResult = Heartbeat_Check(fetcherHb, MONITOR_POLL_SEC);
        if (fetcherHbResult == 1)
            lastFetcherHb = time(NULL);

        int parserHbResult = Heartbeat_Check(parserHb, MONITOR_POLL_SEC);
        if (parserHbResult == 1)
            lastParserHb = time(NULL);

        int serverHbResult = Heartbeat_Check(serverHb, MONITOR_POLL_SEC);
        if (serverHbResult == 1)
            lastServerHb = time(NULL);

        // Check for heartbeat timeouts
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
            status_write("FROZEN process=%s pid=%d\n", frozenName, frozenPid);

            kill(fetcher_pid, SIGTERM);
            kill(parser_pid, SIGTERM);
            kill(server_pid, SIGTERM);
            sleep(2);

            kill(fetcher_pid, SIGKILL);
            kill(parser_pid, SIGKILL);
            kill(server_pid, SIGKILL);

            waitpid(fetcher_pid, &status, 0);
            waitpid(parser_pid, &status, 0);
            waitpid(server_pid, &status, 0);

            goto all_died;
        }

        // Check if any process has died
        pid_t result = waitpid(-1, &status, WNOHANG);

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

        // Identify which process died
        const char *deadProcess = "Unknown";
        if (result == fetcher_pid)
            deadProcess = "Fetcher";
        else if (result == parser_pid)
            deadProcess = "Parser";
        else if (result == server_pid)
            deadProcess = "Server";

        if (WIFEXITED(status))
        {
            int code = WEXITSTATUS(status);
            if (code == 0 && !watchdog_running)
            {
                LOG_INFO("%s exited cleanly (exit 0)", deadProcess);
                status_write("STOP process=%s exit=0\n", deadProcess);
                break;
            }
            LOG_WARNING("Watchdog: %s exited with code %d", deadProcess, code);
            status_write("CRASH process=%s code=%d\n", deadProcess, code);
        }
        else if (WIFSIGNALED(status))
        {
            int sig = WTERMSIG(status);
            if (!watchdog_running)
            {
                LOG_INFO("Watchdog: %s terminated by signal %d during shutdown", deadProcess, sig);
                status_write("STOP process=%s signal=%d\n", deadProcess, sig);
                break;
            }
            LOG_WARNING("Watchdog: %s killed by signal %d (%s)", deadProcess, sig, strsignal(sig));
            status_write("CRASH process=%s signal=%d\n", deadProcess, sig);
        }

        // Kill remaining processes and restart all
        LOG_INFO("Killing all processes due to %s crash", deadProcess);
        kill(fetcher_pid, SIGTERM);
        kill(parser_pid, SIGTERM);
        kill(server_pid, SIGTERM);
        sleep(1);
        waitpid(fetcher_pid, NULL, WNOHANG);
        waitpid(parser_pid, NULL, WNOHANG);
        waitpid(server_pid, NULL, WNOHANG);

        if (!watchdog_running)
            break;

        if (!RestartPolicy_CanRestart(policy))
        {
            LOG_FATAL("Watchdog: Max restarts (%d) exceeded in %d seconds, giving up",
                      RestartPolicy_GetMax(policy), RESTART_WINDOW_SEC);
            status_write("FATAL max_restarts=%d\n", RestartPolicy_GetMax(policy));
            Heartbeat_Destroy(fetcherHb);
            Heartbeat_Destroy(parserHb);
            Heartbeat_Destroy(serverHb);
            RestartPolicy_Destroy(policy);
            CleanupFifos();
            status_close();
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

        CleanupFifos();
        CreateFifos();

        Watchdog_SpawnAll(fetcherPath, parserPath, serverPath,
                          &fetcherHb, &parserHb, &serverHb,
                          (pid_t*)&fetcher_pid, (pid_t*)&parser_pid, (pid_t*)&server_pid);

        if (fetcher_pid < 0 || parser_pid < 0 || server_pid < 0)
        {
            LOG_FATAL("Watchdog: Failed to respawn processes");
            Heartbeat_Destroy(fetcherHb);
            Heartbeat_Destroy(parserHb);
            Heartbeat_Destroy(serverHb);
            RestartPolicy_Destroy(policy);
            CleanupFifos();
            status_close();
            return 1;
        }

        status_write("RESTART attempt=%d/%d fetcher=%d parser=%d server=%d delay=%ds\n",
                     RestartPolicy_GetCount(policy), RestartPolicy_GetMax(policy),
                     (int)fetcher_pid, (int)parser_pid, (int)server_pid, backoff);

        lastFetcherHb = time(NULL);
        lastParserHb = time(NULL);
        lastServerHb = time(NULL);
    }

    LOG_INFO("Waiting for all processes to exit");
    if (fetcher_pid > 0)
        waitpid(fetcher_pid, NULL, 0);
    if (parser_pid > 0)
        waitpid(parser_pid, NULL, 0);
    if (server_pid > 0)
        waitpid(server_pid, NULL, 0);

    Heartbeat_Destroy(fetcherHb);
    Heartbeat_Destroy(parserHb);
    Heartbeat_Destroy(serverHb);
    RestartPolicy_Destroy(policy);
    CleanupFifos();
    status_close();
    LOG_INFO("Watchdog exiting");
    return 0;
}
