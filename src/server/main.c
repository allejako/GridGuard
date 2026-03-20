#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>

#include "server/Server.h"
#include "sys/Logger.h"
#include "sys/Daemon.h"
#include "config/RuntimeConfig.h"

int main(void)
{
    // CHECK IF STARTED BY WATCHDOG
    // WATCHDOG SETS GRIDGUARD_HEARTBEAT_FD env variable
    int daemonize = (getenv("GRIDGUARD_HEARTBEAT_FD") != NULL);

    // RESOLVE LOG PATH FIX FOR DAEMON MODE
    char logPath[PATH_MAX + 64];
    if (daemonize)
    {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == NULL)
        {
            perror("getcwd");
            return EXIT_FAILURE;
        }
        snprintf(logPath, sizeof(logPath), "%s/logs/server.log", cwd);
    }
    else
    {
        snprintf(logPath, sizeof(logPath), "logs/server.log");
    }

    // LOAD RUNTIME CONFIG
    RuntimeConfig_Load(getenv("GRIDGUARD_CONFIG_PATH"));

    // INITIALIZE GRIDGUARD LOGGER
    if (Logger_Initiate(logPath, LOG_LEVEL_DEBUG) != 0)
    {
        fprintf(stderr, "Failed to initialize logger\n");
        return EXIT_FAILURE;
    }

    // DAEMONIZE IF STARTED BY WATCHDOG
    if (daemonize)
    {
        if (Daemon_Initiate() != 0)
        {
            LOG_FATAL("Failed to daemonize");
            Logger_Shutdown();
            return EXIT_FAILURE;
        }
        LOG_INFO("Running as daemon (PID %d)", getpid());
        Daemon_StartHeartbeat();
    }

    // CREATE AND INITIATE THE SERVER
    Server server;
    if (Server_Initiate(&server) != 0)
    {
        LOG_FATAL("Failed to initialize server");
        Logger_Shutdown();
        if (daemonize) Daemon_Shutdown();
        return EXIT_FAILURE;
    }

    // RUN THE SERVER MAIN LOOP 
    Server_Run(&server);

    // CLEANUP AND EXIT
    Server_Shutdown(&server);
    if (daemonize) Daemon_Shutdown();
    Logger_Shutdown();

    return EXIT_SUCCESS;
}
