#define _POSIX_C_SOURCE 200809L

#include "sys/Daemon.h"
#include "sys/PidFile.h"
#include "sys/Logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>

// Heartbeat state
#define HEARTBEAT_INTERVAL_SEC 5

static int heartbeat_fd = -1;
static pthread_t heartbeat_thread;
static volatile sig_atomic_t heartbeat_running = 0;

static void *Daemon_HeartbeatLoop(void *arg)
{
    (void)arg;

    LOG_INFO("Daemon: Heartbeat thread started (fd=%d, interval=%ds)",
             heartbeat_fd, HEARTBEAT_INTERVAL_SEC);

    while (heartbeat_running)
    {
        const char *msg = "heartbeat\n";
        ssize_t written = write(heartbeat_fd, msg, strlen(msg));

        if (written < 0)
        {
            LOG_WARNING("Daemon: Heartbeat write failed, pipe may be broken");
            break;
        }

        // Sleep in 1-second increments so we can check heartbeat_running
        for (int i = 0; i < HEARTBEAT_INTERVAL_SEC && heartbeat_running; i++)
        {
            sleep(1);
        }
    }

    LOG_INFO("Daemon: Heartbeat thread exiting");
    return NULL;
}

int Daemon_Init(void)
{
    // When running under a watchdog (GRIDGUARD_HEARTBEAT_FD is set), skip the
    // double-fork: the watchdog already owns process supervision via waitpid.
    // Forking would make the watchdog track the wrong PID (the intermediate
    // child that exits immediately), leaving the real daemon unsupervised.
    int under_watchdog = (getenv("GRIDGUARD_HEARTBEAT_FD") != NULL);

    if (!under_watchdog)
    {
        // Step 1: Fork and let parent exit
        pid_t pid = fork();
        if (pid < 0)
        {
            perror("Daemon: fork() failed");
            return -1;
        }
        if (pid > 0)
        {
            // Parent exits - child continues
            _exit(0);
        }

        // Step 2: Create new session - become session leader, detach from terminal
        if (setsid() < 0)
        {
            perror("Daemon: setsid() failed");
            return -1;
        }

        // Step 3: Fork again to ensure we can never reacquire a terminal
        pid = fork();
        if (pid < 0)
        {
            perror("Daemon: second fork() failed");
            return -1;
        }
        if (pid > 0)
        {
            // First child exits - grandchild becomes the daemon
            _exit(0);
        }
    }

    // Step 4: Change working directory to root so we don't lock any mountpoints
    if (chdir("/") < 0)
    {
        perror("Daemon: chdir(\"/\") failed");
        return -1;
    }

    // Step 5: Close stdin/stdout/stderr and redirect to /dev/null
    // Note: Do NOT close the heartbeat fd (inherited from watchdog via fork)
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0)
    {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > STDERR_FILENO)
        {
            close(devnull);
        }
    }

    // Step 6: Write PID file
    if (PidFile_Write(GRIDGUARD_PID_FILE) != 0)
    {
        return -1;
    }

    // Step 7: Ignore SIGPIPE (clients disconnecting should not kill daemon)
    signal(SIGPIPE, SIG_IGN);

    return 0;
}

int Daemon_StartHeartbeat(void)
{
    // Read heartbeat fd from environment (set by watchdog before exec)
    const char *fd_env = getenv("GRIDGUARD_HEARTBEAT_FD");
    if (fd_env == NULL)
    {
        LOG_INFO("Daemon: No heartbeat fd set (GRIDGUARD_HEARTBEAT_FD not found), skipping heartbeat");
        return 0;
    }

    heartbeat_fd = atoi(fd_env);
    if (heartbeat_fd < 0)
    {
        LOG_ERROR("Daemon: Invalid heartbeat fd: %s", fd_env);
        return -1;
    }

    LOG_INFO("Daemon: Starting heartbeat thread on fd %d", heartbeat_fd);

    heartbeat_running = 1;
    if (pthread_create(&heartbeat_thread, NULL, Daemon_HeartbeatLoop, NULL) != 0)
    {
        LOG_ERROR("Daemon: Failed to create heartbeat thread");
        heartbeat_running = 0;
        return -1;
    }

    return 0;
}

void Daemon_StopHeartbeat(void)
{
    if (!heartbeat_running)
        return;

    LOG_INFO("Daemon: Stopping heartbeat thread");
    heartbeat_running = 0;
    pthread_join(heartbeat_thread, NULL);

    if (heartbeat_fd >= 0)
    {
        close(heartbeat_fd);
        heartbeat_fd = -1;
    }
}

void Daemon_Cleanup(void)
{
    Daemon_StopHeartbeat();
    PidFile_Remove(GRIDGUARD_PID_FILE);
}
