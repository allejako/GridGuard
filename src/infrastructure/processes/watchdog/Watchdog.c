#define _POSIX_C_SOURCE 200809L

#include "Watchdog.h"
#include "Logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <poll.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#define MAX_RESTARTS        5
#define RESTART_WINDOW_SEC  300
#define BASE_BACKOFF_SEC    2
#define HEARTBEAT_INTERVAL  5
#define HEARTBEAT_TIMEOUT   15
#define MONITOR_POLL_SEC    2

#define STATUS_FIFO_PATH    "/tmp/gridguard.status"

static volatile sig_atomic_t watchdog_running = 1;
static volatile pid_t daemon_pid = -1;

static int heartbeat_pipe[2] = {-1, -1};
static int status_fd = -1;

// ============================================================
// Signal handling
// ============================================================

static void Watchdog_SignalHandler(int signum)
{
    if (signum == SIGTERM || signum == SIGINT)
    {
        watchdog_running = 0;

        if (daemon_pid > 0)
            kill(daemon_pid, SIGTERM);
    }
    else if (signum == SIGHUP)
    {
        if (daemon_pid > 0)
            kill(daemon_pid, SIGHUP);
    }
}

static void Watchdog_SetupSignals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = Watchdog_SignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGHUP,  &sa, NULL);

    signal(SIGPIPE, SIG_IGN);
}

// ============================================================
// Heartbeat pipe
// ============================================================

static int Watchdog_CreateHeartbeatPipe(void)
{
    if (pipe(heartbeat_pipe) < 0)
    {
        LOG_ERROR("Watchdog: pipe() failed: %s", strerror(errno));
        return -1;
    }

    LOG_INFO("Heartbeat pipe created (read=%d, write=%d)",
             heartbeat_pipe[0], heartbeat_pipe[1]);
    return 0;
}

static void Watchdog_CloseHeartbeatPipe(void)
{
    if (heartbeat_pipe[0] >= 0)
    {
        close(heartbeat_pipe[0]);
        heartbeat_pipe[0] = -1;
    }
    if (heartbeat_pipe[1] >= 0)
    {
        close(heartbeat_pipe[1]);
        heartbeat_pipe[1] = -1;
    }
}

static int Watchdog_CheckHeartbeat(int timeout_sec)
{
    if (heartbeat_pipe[0] < 0)
        return 1;

    struct pollfd pfd;
    pfd.fd     = heartbeat_pipe[0];
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, timeout_sec * 1000);

    if (ret < 0)
    {
        if (errno == EINTR)
            return 1;
        LOG_ERROR("Watchdog: poll() failed: %s", strerror(errno));
        return -1;
    }

    if (ret == 0)
        return 0;

    char buf[64];
    ssize_t n = read(heartbeat_pipe[0], buf, sizeof(buf));
    if (n <= 0)
        return -1;

    return 1;
}

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
// Daemon spawning
// ============================================================

static pid_t Watchdog_SpawnDaemon(const char *daemon_path)
{
    Watchdog_CloseHeartbeatPipe();
    if (Watchdog_CreateHeartbeatPipe() != 0)
        LOG_WARNING("Watchdog: Continuing without heartbeat pipe");

    pid_t pid = fork();

    if (pid < 0)
    {
        LOG_ERROR("Watchdog: fork() failed: %s", strerror(errno));
        return -1;
    }

    if (pid == 0)
    {
        if (heartbeat_pipe[0] >= 0)
            close(heartbeat_pipe[0]);

        if (heartbeat_pipe[1] >= 0)
        {
            char fd_str[16];
            snprintf(fd_str, sizeof(fd_str), "%d", heartbeat_pipe[1]);
            setenv("GRIDGUARD_HEARTBEAT_FD", fd_str, 1);
        }

        execl(daemon_path, "GridGuard-server", NULL);

        fprintf(stderr, "Watchdog: execl(%s) failed: %s\n", daemon_path, strerror(errno));
        _exit(127);
    }

    if (heartbeat_pipe[1] >= 0)
    {
        close(heartbeat_pipe[1]);
        heartbeat_pipe[1] = -1;
    }

    return pid;
}

// ============================================================
// Restart logic
// ============================================================

typedef struct
{
    int    count;
    time_t first_restart;
    time_t timestamps[MAX_RESTARTS];
} RestartTracker;

static void RestartTracker_Init(RestartTracker *rt)
{
    memset(rt, 0, sizeof(RestartTracker));
}

static int RestartTracker_CanRestart(RestartTracker *rt)
{
    time_t now = time(NULL);

    if (rt->count > 0 && difftime(now, rt->first_restart) > RESTART_WINDOW_SEC)
    {
        LOG_INFO("Watchdog: Restart window expired, resetting counter");
        rt->count = 0;
    }

    return rt->count < MAX_RESTARTS;
}

static void RestartTracker_Record(RestartTracker *rt)
{
    time_t now = time(NULL);

    if (rt->count == 0)
        rt->first_restart = now;

    if (rt->count < MAX_RESTARTS)
        rt->timestamps[rt->count] = now;

    rt->count++;
}

static int RestartTracker_GetBackoff(RestartTracker *rt)
{
    int delay = BASE_BACKOFF_SEC;
    for (int i = 0; i < rt->count - 1 && delay < 32; i++)
        delay *= 2;
    return delay;
}

// ============================================================
// Main watchdog loop
// ============================================================

int Watchdog_Run(const char *daemon_path)
{
    Watchdog_SetupSignals();
    status_open();

    RestartTracker tracker;
    RestartTracker_Init(&tracker);

    LOG_INFO("Starting daemon: %s", daemon_path);

    daemon_pid = Watchdog_SpawnDaemon(daemon_path);
    if (daemon_pid < 0)
    {
        LOG_FATAL("Watchdog: Failed to spawn daemon");
        status_close();
        return 1;
    }

    LOG_INFO("Daemon started (PID %d)", daemon_pid);
    status_write("START pid=%d\n", (int)daemon_pid);

    int status;
    time_t last_heartbeat  = time(NULL);
    int killed_for_timeout = 0;

    while (watchdog_running)
    {
        int hb = Watchdog_CheckHeartbeat(MONITOR_POLL_SEC);
        if (hb == 1)
        {
            last_heartbeat = time(NULL);
        }
        else if (hb == 0)
        {
            double elapsed = difftime(time(NULL), last_heartbeat);
            if (elapsed < HEARTBEAT_TIMEOUT)
                goto check_waitpid;

            LOG_WARNING("Watchdog: No heartbeat for %.0f seconds (timeout=%d), daemon may be frozen",
                        elapsed, HEARTBEAT_TIMEOUT);
            status_write("FROZEN elapsed=%.0fs\n", elapsed);

            killed_for_timeout = 1;
            LOG_INFO("Sending SIGTERM to frozen daemon (PID %d)", daemon_pid);
            kill(daemon_pid, SIGTERM);
            sleep(5);

            pid_t result = waitpid(daemon_pid, &status, WNOHANG);
            if (result == 0)
            {
                LOG_WARNING("Watchdog: Daemon didn't respond to SIGTERM, sending SIGKILL");
                kill(daemon_pid, SIGKILL);
                waitpid(daemon_pid, &status, 0);
            }

            goto daemon_died;
        }

        check_waitpid:;
        pid_t result = waitpid(daemon_pid, &status, WNOHANG);

        if (result < 0)
        {
            if (errno == EINTR)
                continue;
            LOG_ERROR("Watchdog: waitpid() failed: %s", strerror(errno));
            break;
        }

        if (result == 0)
            continue;

        daemon_died:

        if (WIFEXITED(status))
        {
            int code = WEXITSTATUS(status);
            if (code == 0 && !killed_for_timeout)
            {
                LOG_INFO("Daemon exited cleanly (exit 0)");
                status_write("STOP exit=0\n");
                Watchdog_CloseHeartbeatPipe();
                status_close();
                return 0;
            }
            if (code != 0)
            {
                LOG_WARNING("Watchdog: Daemon exited with error code %d", code);
                status_write("CRASH code=%d\n", code);
            }
            else
            {
                LOG_WARNING("Watchdog: Frozen daemon exited cleanly after SIGTERM, restarting");
            }
        }
        else if (WIFSIGNALED(status))
        {
            int sig = WTERMSIG(status);
            if (!watchdog_running)
            {
                LOG_INFO("Watchdog: Daemon terminated by signal %d during shutdown", sig);
                status_write("STOP signal=%d\n", sig);
                Watchdog_CloseHeartbeatPipe();
                status_close();
                return 0;
            }
            LOG_WARNING("Watchdog: Daemon killed by signal %d (%s)", sig, strsignal(sig));
            status_write("CRASH signal=%d\n", sig);
        }

        if (!watchdog_running)
            break;

        if (!RestartTracker_CanRestart(&tracker))
        {
            LOG_FATAL("Watchdog: Max restarts (%d) exceeded in %d seconds, giving up",
                      MAX_RESTARTS, RESTART_WINDOW_SEC);
            status_write("FATAL max_restarts=%d\n", MAX_RESTARTS);
            Watchdog_CloseHeartbeatPipe();
            status_close();
            return 1;
        }

        RestartTracker_Record(&tracker);
        int backoff = RestartTracker_GetBackoff(&tracker);

        LOG_INFO("Restarting daemon in %d seconds (attempt %d/%d)",
                 backoff, tracker.count, MAX_RESTARTS);

        for (int i = 0; i < backoff && watchdog_running; i++)
            sleep(1);

        if (!watchdog_running)
            break;

        daemon_pid = Watchdog_SpawnDaemon(daemon_path);
        if (daemon_pid < 0)
        {
            LOG_FATAL("Watchdog: Failed to respawn daemon");
            status_close();
            Watchdog_CloseHeartbeatPipe();
            return 1;
        }

        LOG_INFO("Daemon restarted (PID %d)", daemon_pid);
        status_write("RESTART attempt=%d/%d pid=%d delay=%ds\n",
                     tracker.count, MAX_RESTARTS, (int)daemon_pid, backoff);

        last_heartbeat = time(NULL);
        killed_for_timeout = 0;
    }

    if (daemon_pid > 0)
    {
        LOG_INFO("Waiting for daemon");
        waitpid(daemon_pid, &status, 0);
        LOG_INFO("Daemon stopped");
    }

    Watchdog_CloseHeartbeatPipe();
    status_close();
    LOG_INFO("Watchdog exiting");
    return 0;
}
