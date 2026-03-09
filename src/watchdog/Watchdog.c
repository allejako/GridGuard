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

#define MONITOR_POLL_SEC    2
#define STATUS_FIFO_PATH    "/tmp/gridguard.status"

volatile sig_atomic_t watchdog_running = 1;
volatile pid_t        daemon_pid       = -1;

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
// Daemon spawning
// ============================================================

static pid_t Watchdog_SpawnDaemon(const char *daemon_path, Heartbeat **hb_out)
{
    Heartbeat_Destroy(*hb_out);
    *hb_out = Heartbeat_Create();
    if (!*hb_out)
        LOG_WARNING("Watchdog: Continuing without heartbeat pipe");

    pid_t pid = fork();

    if (pid < 0)
    {
        LOG_ERROR("Watchdog: fork() failed: %s", strerror(errno));
        return -1;
    }

    if (pid == 0)
    {
        // Child: close read end, pass write fd via env
        if (*hb_out)
        {
            Heartbeat_CloseReadFd(*hb_out);
            int write_fd = Heartbeat_GetWriteFd(*hb_out);
            char fd_str[16];
            snprintf(fd_str, sizeof(fd_str), "%d", write_fd);
            setenv("GRIDGUARD_HEARTBEAT_FD", fd_str, 1);
        }

        execl(daemon_path, "GridGuard-server", NULL);

        fprintf(stderr, "Watchdog: execl(%s) failed: %s\n", daemon_path, strerror(errno));
        _exit(127);
    }

    // Parent: close write end
    if (*hb_out)
        Heartbeat_CloseWriteFd(*hb_out);

    return pid;
}

// ============================================================
// Main watchdog loop
// ============================================================

int Watchdog_Run(const char *daemon_path)
{
    WatchdogSignals_Setup();
    status_open();

    RestartPolicy *policy = RestartPolicy_Create(MAX_RESTARTS, RESTART_WINDOW_SEC,
                                                 BASE_BACKOFF_SEC);
    if (!policy)
    {
        LOG_FATAL("Watchdog: Failed to create restart policy");
        status_close();
        return 1;
    }

    Heartbeat *hb = NULL;

    LOG_INFO("Starting daemon: %s", daemon_path);

    daemon_pid = Watchdog_SpawnDaemon(daemon_path, &hb);
    if (daemon_pid < 0)
    {
        LOG_FATAL("Watchdog: Failed to spawn daemon");
        RestartPolicy_Destroy(policy);
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
        int hb_result = Heartbeat_Check(hb, MONITOR_POLL_SEC);
        if (hb_result == 1)
        {
            last_heartbeat = time(NULL);
        }
        else if (hb_result == 0)
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
                Heartbeat_Destroy(hb);
                RestartPolicy_Destroy(policy);
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
                Heartbeat_Destroy(hb);
                RestartPolicy_Destroy(policy);
                status_close();
                return 0;
            }
            LOG_WARNING("Watchdog: Daemon killed by signal %d (%s)", sig, strsignal(sig));
            status_write("CRASH signal=%d\n", sig);
        }

        if (!watchdog_running)
            break;

        if (!RestartPolicy_CanRestart(policy))
        {
            LOG_FATAL("Watchdog: Max restarts (%d) exceeded in %d seconds, giving up",
                      RestartPolicy_GetMax(policy), RESTART_WINDOW_SEC);
            status_write("FATAL max_restarts=%d\n", RestartPolicy_GetMax(policy));
            Heartbeat_Destroy(hb);
            RestartPolicy_Destroy(policy);
            status_close();
            return 1;
        }

        RestartPolicy_RecordRestart(policy);
        int backoff = RestartPolicy_GetBackoffDelay(policy);

        LOG_INFO("Restarting daemon in %d seconds (attempt %d/%d)",
                 backoff, RestartPolicy_GetCount(policy), RestartPolicy_GetMax(policy));

        for (int i = 0; i < backoff && watchdog_running; i++)
            sleep(1);

        if (!watchdog_running)
            break;

        daemon_pid = Watchdog_SpawnDaemon(daemon_path, &hb);
        if (daemon_pid < 0)
        {
            LOG_FATAL("Watchdog: Failed to respawn daemon");
            Heartbeat_Destroy(hb);
            RestartPolicy_Destroy(policy);
            status_close();
            return 1;
        }

        LOG_INFO("Daemon restarted (PID %d)", daemon_pid);
        status_write("RESTART attempt=%d/%d pid=%d delay=%ds\n",
                     RestartPolicy_GetCount(policy), RestartPolicy_GetMax(policy),
                     (int)daemon_pid, backoff);

        last_heartbeat = time(NULL);
        killed_for_timeout = 0;
    }

    if (daemon_pid > 0)
    {
        LOG_INFO("Waiting for daemon");
        waitpid(daemon_pid, &status, 0);
        LOG_INFO("Daemon stopped");
    }

    Heartbeat_Destroy(hb);
    RestartPolicy_Destroy(policy);
    status_close();
    LOG_INFO("Watchdog exiting");
    return 0;
}
