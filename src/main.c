#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>

#include "Server.h"
#include "Logger.h"
#include "Daemon.h"

int main(void)
{
    // Check if started by watchdog, watchdog sets GRIDGUARD_HEARTBEAT_FD env variable
    int daemonize = (getenv("GRIDGUARD_HEARTBEAT_FD") != NULL);

    // Resolve log path
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

    // Initialize logger
    if (Logger_Initiate(log_path, LOG_LEVEL_DEBUG) != 0)
    {
        fprintf(stderr, "Failed to initialize logger\n");
        return EXIT_FAILURE;
    }

    // Daemonize if started by watchdog
    if (daemonize)
    {
        if (Daemon_Init() != 0)
        {
            LOG_FATAL("Failed to daemonize");
            Logger_Shutdown();
            return EXIT_FAILURE;
        }
        LOG_INFO("Running as daemon (PID %d)", getpid());
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
