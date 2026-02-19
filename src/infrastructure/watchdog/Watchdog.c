#define _POSIX_C_SOURCE 200809L

#include "Watchdog.h"
#include "Logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <string.h>
#include <errno.h>
#include <time.h>

// Restart policy
#define MAX_RESTARTS        5
#define RESTART_WINDOW_SEC  300   // 5 minutes
#define BASE_BACKOFF_SEC    2
#define HEARTBEAT_INTERVAL  5     // Daemon writes every 5s
#define HEARTBEAT_TIMEOUT   15    // Watchdog considers daemon frozen after 15s
#define MONITOR_POLL_SEC    2     // waitpid poll interval

static volatile sig_atomic_t watchdog_running = 1;
static volatile sig_atomic_t got_sighup = 0;
static volatile pid_t daemon_pid = -1;

// Heartbeat pipe: daemon writes to pipe_fds[1], watchdog reads from pipe_fds[0]
static int heartbeat_pipe[2] = {-1, -1};

// ============================================================
// Signal handling
// ============================================================

static void Watchdog_SignalHandler(int signum)
{
    if (signum == SIGTERM || signum == SIGINT)
    {
        watchdog_running = 0;

        // Forward SIGTERM to daemon child
        if (daemon_pid > 0)
        {
            kill(daemon_pid, SIGTERM);
        }
    }
    else if (signum == SIGHUP)
    {
        got_sighup = 1;

        // Forward SIGHUP to daemon for config reload
        if (daemon_pid > 0)
        {
            kill(daemon_pid, SIGHUP);
        }
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
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    // Ignore SIGPIPE (broken pipe from heartbeat)
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

    LOG_INFO("Watchdog: Heartbeat pipe created (read_fd=%d, write_fd=%d)",
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

// Check heartbeat pipe with timeout. Returns 1 if heartbeat received, 0 on timeout, -1 on error.
static int Watchdog_CheckHeartbeat(int timeout_sec)
{
    if (heartbeat_pipe[0] < 0)
    {
        return 1; // No pipe = skip heartbeat check
    }

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(heartbeat_pipe[0], &readfds);

    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;

    int ret = select(heartbeat_pipe[0] + 1, &readfds, NULL, NULL, &tv);

    if (ret < 0)
    {
        if (errno == EINTR)
            return 1; // Interrupted by signal, not a timeout
        LOG_ERROR("Watchdog: select() on heartbeat pipe failed: %s", strerror(errno));
        return -1;
    }

    if (ret == 0)
    {
        return 0; // Timeout - no heartbeat
    }

    // Read and discard heartbeat data
    char buf[64];
    ssize_t n = read(heartbeat_pipe[0], buf, sizeof(buf));
    if (n <= 0)
    {
        return -1; // Pipe closed or error
    }

    return 1; // Heartbeat received
}

// ============================================================
// Daemon spawning
// ============================================================

static pid_t Watchdog_SpawnDaemon(const char *daemon_path)
{
    // Create new heartbeat pipe for this daemon instance
    Watchdog_CloseHeartbeatPipe();
    if (Watchdog_CreateHeartbeatPipe() != 0)
    {
        LOG_WARNING("Watchdog: Continuing without heartbeat pipe");
    }

    pid_t pid = fork();

    if (pid < 0)
    {
        LOG_ERROR("Watchdog: fork() failed: %s", strerror(errno));
        return -1;
    }

    if (pid == 0)
    {
        // Child process

        // Close read end of pipe (child only writes)
        if (heartbeat_pipe[0] >= 0)
        {
            close(heartbeat_pipe[0]);
        }

        // Pass write-fd to daemon via environment variable
        if (heartbeat_pipe[1] >= 0)
        {
            char fd_str[16];
            snprintf(fd_str, sizeof(fd_str), "%d", heartbeat_pipe[1]);
            setenv("GRIDGUARD_HEARTBEAT_FD", fd_str, 1);
        }

        // exec the daemon with -d flag
        execl(daemon_path, "GridGuard-server", "-d", NULL);

        // If we get here, exec failed
        fprintf(stderr, "Watchdog: execl(%s) failed: %s\n", daemon_path, strerror(errno));
        _exit(127);
    }

    // Parent: close write end of pipe (parent only reads)
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
    int count;
    time_t first_restart;
    time_t timestamps[MAX_RESTARTS];
} RestartTracker;

static void RestartTracker_Init(RestartTracker *rt)
{
    memset(rt, 0, sizeof(RestartTracker));
}

// Returns 1 if restart is allowed, 0 if limit exceeded.
static int RestartTracker_CanRestart(RestartTracker *rt)
{
    time_t now = time(NULL);

    // If first restart was more than RESTART_WINDOW_SEC ago, reset the counter
    if (rt->count > 0 && difftime(now, rt->first_restart) > RESTART_WINDOW_SEC)
    {
        LOG_INFO("Watchdog: Restart window expired, resetting counter");
        rt->count = 0;
    }

    if (rt->count >= MAX_RESTARTS)
    {
        return 0; // Too many restarts
    }

    return 1;
}

static void RestartTracker_Record(RestartTracker *rt)
{
    time_t now = time(NULL);

    if (rt->count == 0)
    {
        rt->first_restart = now;
    }

    if (rt->count < MAX_RESTARTS)
    {
        rt->timestamps[rt->count] = now;
    }
    rt->count++;
}

// Calculate backoff delay: 2^count * BASE_BACKOFF_SEC (capped at 32s)
static int RestartTracker_GetBackoff(RestartTracker *rt)
{
    int delay = BASE_BACKOFF_SEC;
    for (int i = 0; i < rt->count - 1 && delay < 32; i++)
    {
        delay *= 2;
    }
    return delay;
}

// ============================================================
// Main watchdog loop
// ============================================================

int Watchdog_Run(const char *daemon_path)
{
    Watchdog_SetupSignals();

    RestartTracker tracker;
    RestartTracker_Init(&tracker);

    LOG_INFO("Watchdog: Starting daemon from %s", daemon_path);

    // Initial spawn
    daemon_pid = Watchdog_SpawnDaemon(daemon_path);
    if (daemon_pid < 0)
    {
        LOG_FATAL("Watchdog: Failed to spawn daemon");
        return 1;
    }

    LOG_INFO("Watchdog: Daemon spawned with PID %d", daemon_pid);

    int status;
    time_t last_stable_start = time(NULL);

    while (watchdog_running)
    {
        // Handle SIGHUP (config reload) - already forwarded in signal handler
        if (got_sighup)
        {
            LOG_INFO("Watchdog: SIGHUP received, config reload forwarded to daemon");
            got_sighup = 0;
        }

        // Check heartbeat (non-blocking with timeout)
        int hb = Watchdog_CheckHeartbeat(MONITOR_POLL_SEC);
        if (hb == 0)
        {
            LOG_WARNING("Watchdog: No heartbeat for %d seconds, daemon may be frozen",
                        HEARTBEAT_TIMEOUT);

            // Send SIGTERM first, give it 5 seconds
            LOG_INFO("Watchdog: Sending SIGTERM to frozen daemon (PID %d)", daemon_pid);
            kill(daemon_pid, SIGTERM);
            sleep(5);

            // Check if it died
            pid_t result = waitpid(daemon_pid, &status, WNOHANG);
            if (result == 0)
            {
                // Still alive after SIGTERM, force kill
                LOG_WARNING("Watchdog: Daemon didn't respond to SIGTERM, sending SIGKILL");
                kill(daemon_pid, SIGKILL);
                waitpid(daemon_pid, &status, 0);
            }

            // Fall through to restart logic below
            goto daemon_died;
        }

        // Non-blocking check if daemon has exited
        pid_t result = waitpid(daemon_pid, &status, WNOHANG);

        if (result < 0)
        {
            if (errno == EINTR)
                continue;
            LOG_ERROR("Watchdog: waitpid() failed: %s", strerror(errno));
            break;
        }

        if (result == 0)
        {
            // Daemon still running
            continue;
        }

        // Daemon has exited
        daemon_died:

        if (WIFEXITED(status))
        {
            int exit_code = WEXITSTATUS(status);
            if (exit_code == 0)
            {
                LOG_INFO("Watchdog: Daemon exited cleanly (code 0), shutting down");
                Watchdog_CloseHeartbeatPipe();
                return 0;
            }
            LOG_WARNING("Watchdog: Daemon exited with error code %d", exit_code);
        }
        else if (WIFSIGNALED(status))
        {
            int sig = WTERMSIG(status);
            // SIGTERM during our shutdown is expected, don't restart
            if (!watchdog_running)
            {
                LOG_INFO("Watchdog: Daemon terminated by signal %d during shutdown", sig);
                Watchdog_CloseHeartbeatPipe();
                return 0;
            }
            LOG_WARNING("Watchdog: Daemon killed by signal %d (%s)", sig, strsignal(sig));
        }

        // Should we restart?
        if (!watchdog_running)
            break;

        if (!RestartTracker_CanRestart(&tracker))
        {
            LOG_FATAL("Watchdog: Max restarts (%d) exceeded in %d seconds, giving up",
                      MAX_RESTARTS, RESTART_WINDOW_SEC);
            Watchdog_CloseHeartbeatPipe();
            return 1;
        }

        RestartTracker_Record(&tracker);
        int backoff = RestartTracker_GetBackoff(&tracker);

        LOG_INFO("Watchdog: Restarting daemon in %d seconds (attempt %d/%d)",
                 backoff, tracker.count, MAX_RESTARTS);

        // Backoff sleep (interruptible)
        for (int i = 0; i < backoff && watchdog_running; i++)
        {
            sleep(1);
        }

        if (!watchdog_running)
            break;

        // Spawn new daemon
        daemon_pid = Watchdog_SpawnDaemon(daemon_path);
        if (daemon_pid < 0)
        {
            LOG_FATAL("Watchdog: Failed to respawn daemon");
            Watchdog_CloseHeartbeatPipe();
            return 1;
        }

        LOG_INFO("Watchdog: Daemon respawned with PID %d", daemon_pid);
        last_stable_start = time(NULL);
    }

    // Shutting down: wait for daemon
    if (daemon_pid > 0)
    {
        LOG_INFO("Watchdog: Waiting for daemon to shut down...");
        waitpid(daemon_pid, &status, 0);
        LOG_INFO("Watchdog: Daemon stopped");
    }

    Watchdog_CloseHeartbeatPipe();
    LOG_INFO("Watchdog: Exiting");
    (void)last_stable_start; // Used for future stable-run tracking
    return 0;
}
