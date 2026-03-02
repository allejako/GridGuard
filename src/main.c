#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "Server.h"
#include "Logger.h"
#include "Daemon.h"

static void print_usage(const char *progname)
{
    fprintf(stderr, "Usage: %s [OPTIONS]\n", progname);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -d, --daemon          run as daemon\n");
    fprintf(stderr, "  -h, --help            show this help\n");
}

int main(int argc, char *argv[])
{
    int daemonize = 0;

    // Parse arguments
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--daemon") == 0)
        {
            daemonize = 1;
        }
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        }
        else
        {
            fprintf(stderr, "Error: Unknown option '%s'\n\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    // Resolve log path innan daemonizing
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

    // Daemonize if requested
    if (daemonize)
    {
        if (Daemon_Init() != 0)
        {
            LOG_FATAL("Failed to daemonize");
            Logger_Shutdown();
            return EXIT_FAILURE;
        }
        LOG_INFO("Server: Running as daemon (PID %d)", getpid());
        Daemon_StartHeartbeat();
    }

    LOG_INFO("GridGuard: Process-based IPC architecture");
    LOG_INFO("  Demonstrating: fork/exec, pipes, FIFO, Unix sockets, shm, semaphores");

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
