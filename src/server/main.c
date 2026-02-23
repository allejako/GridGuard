#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "Server.h"
#include "Logger.h"
#include "Daemon.h"

int main(int argc, char *argv[])
{
    int daemonize = 0;

    // Parse arguments
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-d") == 0)
        {
            daemonize = 1;
        }
    }

    // Resolve log path to absolute BEFORE daemonizing (cwd changes to /)
    char log_path[PATH_MAX + 64];
    if (daemonize)
    {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == NULL)
        {
            perror("getcwd");
            return EXIT_FAILURE;
        }
        snprintf(log_path, sizeof(log_path), "%s/logs/server.log", cwd);
    }
    else
    {
        snprintf(log_path, sizeof(log_path), "logs/server.log");
    }

    // Initialize logger before daemonizing so PidFile_Write errors are captured.
    // The log path is already absolute (resolved above), so chdir("/") won't affect it.
    if (Logger_Initiate(log_path, LOG_LEVEL_DEBUG) != 0)
    {
        fprintf(stderr, "Failed to initialize logger\n");
        return EXIT_FAILURE;
    }

    // Daemonize (stdout/stderr redirected to /dev/null; file logger stays open)
    if (daemonize)
    {
        if (Daemon_Init() != 0)
        {
            LOG_FATAL("Failed to daemonize");
            Logger_Shutdown();
            return EXIT_FAILURE;
        }
    }

    if (daemonize)
    {
        LOG_INFO("Server: Running as daemon (PID %d)", getpid());

        // Start heartbeat thread (sends heartbeat to watchdog via pipe)
        Daemon_StartHeartbeat();
    }

    // Create and initialize server
    Server server;
    if (Server_Initiate(&server) != 0)
    {
        LOG_FATAL("Failed to initialize server");
        Logger_Shutdown();
        if (daemonize) Daemon_Cleanup();
        return EXIT_FAILURE;
    }

    // Run server main loop
    Server_Run(&server);

    // Cleanup
    Server_Shutdown(&server);
    if (daemonize) Daemon_Cleanup();
    Logger_Shutdown();

    return EXIT_SUCCESS;
}
