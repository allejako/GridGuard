#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "watchdog/Watchdog.h"
#include "sys/Logger.h"
#include "sys/PidFile.h"
#include "sys/Daemon.h"
#include "config/RuntimeConfig.h"

int main(int argc, char *argv[])
{
    const char *fetcherPath = "bin/GridGuard-fetcher";
    const char *parserPath = "bin/GridGuard-parser";
    const char *serverPath = "bin/GridGuard-server";
    const char *configPath = "config/gridguard.conf";
    int daemon_mode = 0;

    // Parse command line arguments
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--daemon") == 0 || strcmp(argv[i], "-d") == 0)
        {
            daemon_mode = 1;
        }
        else if (strcmp(argv[i], "--config") == 0 || strcmp(argv[i], "-c") == 0)
        {
            if (i + 1 < argc)
            {
                configPath = argv[++i];
            }
            else
            {
                fprintf(stderr, "Error: --config requires a path argument\n");
                return 1;
            }
        }
        else
        {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            fprintf(stderr, "Usage: %s [--daemon|-d] [--config|-c PATH]\n", argv[0]);
            return 1;
        }
    }

    if (Logger_Initiate("logs/watchdog.log", LOG_LEVEL_DEBUG) != 0)
    {
        fprintf(stderr, "Failed to initialize logger\n");
        return 1;
    }

    // Load runtime configuration (optional - falls back to defaults)
    if (RuntimeConfig_Load(configPath) != 0)
    {
        LOG_WARNING("Watchdog: Could not load config from %s, using defaults", configPath);
    }

    if (daemon_mode)
    {
        LOG_INFO("Watchdog: Daemonizing...");
        if (Daemon_Initiate() != 0)
        {
            LOG_FATAL("Watchdog: Failed to daemonize");
            Logger_Shutdown();
            return 1;
        }
    }

    // Watchdog is the main process - write PID file
    if (PidFile_Write(GRIDGUARD_PID_FILE) != 0) 
    {
        LOG_FATAL("Watchdog: Failed to write PID file");
        Logger_Shutdown();
        return 1;
    }

    LOG_INFO("Watchdog: Starting with fetcher=%s, parser=%s, server=%s", fetcherPath, parserPath, serverPath);
    int result = Watchdog_Run(fetcherPath, parserPath, serverPath);

    RuntimeConfig_Shutdown();
    PidFile_Remove(GRIDGUARD_PID_FILE);
    Logger_Shutdown();

    return result;
}
