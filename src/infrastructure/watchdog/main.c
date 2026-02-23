#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Watchdog.h"
#include "Logger.h"

#define DEFAULT_DAEMON_PATH "bin/GridGuard-server"

static void print_usage(const char *progname)
{
    fprintf(stderr, "Usage: %s [--daemon-path PATH]\n", progname);
    fprintf(stderr, "  --daemon-path PATH  Path to GridGuard-server binary (default: %s)\n",
            DEFAULT_DAEMON_PATH);
}

int main(int argc, char *argv[])
{
    const char *daemon_path = DEFAULT_DAEMON_PATH;

    // Parse arguments
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--daemon-path") == 0 && i + 1 < argc)
        {
            daemon_path = argv[++i];
        }
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
        {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        }
        else
        {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    // Initialize logger
    if (Logger_Initiate("logs/watchdog.log", LOG_LEVEL_DEBUG) != 0)
    {
        fprintf(stderr, "Failed to initialize logger\n");
        return EXIT_FAILURE;
    }

    LOG_INFO("Watchdog: Starting with daemon path: %s", daemon_path);

    int result = Watchdog_Run(daemon_path);

    Logger_Shutdown();
    return result;
}
