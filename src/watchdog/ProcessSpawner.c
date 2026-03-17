#define _POSIX_C_SOURCE 200809L

#include "watchdog/ProcessSpawner.h"
#include "watchdog/IPCPaths.h"
#include "sys/Logger.h"

#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdio.h>

static pid_t spawnProcess(const char *path, const char *name, char *const argv[], ManagedProcess *proc)
{
    // Shutdown old heartbeat if it was initialized
    if (proc->heartbeatInitialized)
    {
        Heartbeat_Shutdown(&proc->heartbeat);
        proc->heartbeatInitialized = 0;
    }

    if (Heartbeat_Initiate(&proc->heartbeat) != 0)
    {
        LOG_WARNING("ProcessSpawner: Continuing without %s heartbeat pipe", name);
        proc->heartbeatInitialized = 0;
    }
    else
    {
        proc->heartbeatInitialized = 1;
    }

    // fork() duplicates current process. Parent gets child PID, child gets 0.
    pid_t pid = fork();

    if (pid < 0)
    {
        LOG_ERROR("ProcessSpawner: fork() failed for %s: %s", name, strerror(errno));

        // Cleanup heartbeat on fork failure
        if (proc->heartbeatInitialized)
        {
            Heartbeat_Shutdown(&proc->heartbeat);
            proc->heartbeatInitialized = 0;
        }

        return -1;
    }

    if (pid == 0)
    {
        // Child process: Pass heartbeat FD via environment
        if (proc->heartbeatInitialized)
        {
            Heartbeat_CloseReadFd(&proc->heartbeat);
            int writeFd = Heartbeat_GetWriteFd(&proc->heartbeat);
            char fdStr[16];
            snprintf(fdStr, sizeof(fdStr), "%d", writeFd);
            setenv("GRIDGUARD_HEARTBEAT_FD", fdStr, 1);
        }

        // execv() replaces process image. Never returns on success.
        execv(path, argv);

        // Only reached if execv() failed
        fprintf(stderr, "ProcessSpawner: execv(%s) failed: %s\n", path, strerror(errno));

        // _exit() instead of exit() to avoid flushing parent's stdio buffers
        _exit(127);
    }

    // Parent process: Close write end, keep read end for monitoring
    if (proc->heartbeatInitialized)
        Heartbeat_CloseWriteFd(&proc->heartbeat);

    return pid;
}

void ProcessGroup_Initiate(ProcessGroup *group, const char *fetcherPath, const char *parserPath, const char *serverPath)
{
    group->fetcher.pid = -1;
    group->fetcher.heartbeatInitialized = 0;
    group->fetcher.name = "Fetcher";
    group->fetcher.path = fetcherPath;

    group->parser.pid = -1;
    group->parser.heartbeatInitialized = 0;
    group->parser.name = "Parser";
    group->parser.path = parserPath;

    group->server.pid = -1;
    group->server.heartbeatInitialized = 0;
    group->server.name = "Server";
    group->server.path = serverPath;
}

// Spawn all processes in coordinated order
int ProcessGroup_SpawnAll(ProcessGroup *group)
{
    // Start Parser first so it opens read end of FIFO before Fetcher tries to open write end
    char *parser_argv[] = 
    {
        "GridGuard-parser",
        FETCH_TO_PARSE_FIFO_PATH,
        PARSE_TO_COMPUTE_SOCK_PATH,
        PARSE_TO_COMPUTE_NOTIFY_PATH,
        NULL
    };

    group->parser.pid = spawnProcess(group->parser.path, group->parser.name, parser_argv, &group->parser);
    if (group->parser.pid < 0)
    {
        LOG_ERROR("ProcessSpawner: Failed to spawn parser");
        return -1;
    }
    LOG_INFO("Parser started (PID %d)", group->parser.pid);

    sleep(1);  // Give parser time to open FIFO read end

    // Start Fetcher
    char *fetcher_argv[] =
    {
        "GridGuard-fetcher",
        REQUEST_FIFO_PATH,
        FETCH_TO_PARSE_FIFO_PATH,
        NULL
    };

    group->fetcher.pid = spawnProcess(group->fetcher.path, group->fetcher.name, fetcher_argv, &group->fetcher);
    if (group->fetcher.pid < 0)
    {
        LOG_ERROR("ProcessSpawner: Failed to spawn fetcher");
        kill(group->parser.pid, SIGTERM);
        return -1;
    }
    LOG_INFO("Fetcher started (PID %d)", group->fetcher.pid);

    sleep(1);

    // Start Server
    char *server_argv[] =
    {
        "GridGuard-server",
        NULL
    };

    group->server.pid = spawnProcess(group->server.path, group->server.name, server_argv, &group->server);
    if (group->server.pid < 0)
    {
        LOG_ERROR("ProcessSpawner: Failed to spawn server");
        kill(group->fetcher.pid, SIGTERM);
        kill(group->parser.pid, SIGTERM);
        return -1;
    }
    LOG_INFO("Server started (PID %d)", group->server.pid);

    return 0;
}

// Send signal to all running processes
void ProcessGroup_KillAll(ProcessGroup *group, int signal)
{
    if (group->fetcher.pid > 0)
        kill(group->fetcher.pid, signal);
    if (group->parser.pid > 0)
        kill(group->parser.pid, signal);
    if (group->server.pid > 0)
        kill(group->server.pid, signal);
}

// Wait for all processes to terminate and reap them to prevent zombies.
// waitpid() blocks until specified child exits and frees its resources.
void ProcessGroup_WaitAll(ProcessGroup *group)
{
    if (group->fetcher.pid > 0)
        waitpid(group->fetcher.pid, NULL, 0);
    if (group->parser.pid > 0)
        waitpid(group->parser.pid, NULL, 0);
    if (group->server.pid > 0)
        waitpid(group->server.pid, NULL, 0);
}

// Cleanup heartbeats and reset PIDs
void ProcessGroup_Cleanup(ProcessGroup *group)
{
    if (group->fetcher.heartbeatInitialized)
    {
        Heartbeat_Shutdown(&group->fetcher.heartbeat);
        group->fetcher.heartbeatInitialized = 0;
    }

    if (group->parser.heartbeatInitialized)
    {
        Heartbeat_Shutdown(&group->parser.heartbeat);
        group->parser.heartbeatInitialized = 0;
    }

    if (group->server.heartbeatInitialized)
    {
        Heartbeat_Shutdown(&group->server.heartbeat);
        group->server.heartbeatInitialized = 0;
    }

    group->fetcher.pid = -1;
    group->parser.pid = -1;
    group->server.pid = -1;
}
