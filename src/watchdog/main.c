#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "watchdog/Watchdog.h"
#include "sys/Logger.h"

int main(int argc, char *argv[])
{
    const char *fetcherPath = "bin/GridGuard-fetcher";
    const char *parserPath = "bin/GridGuard-parser";
    const char *serverPath = "bin/GridGuard-server";

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--fetcher-path") == 0 && i + 1 < argc)
            fetcherPath = argv[++i];
        else if (strcmp(argv[i], "--parser-path") == 0 && i + 1 < argc)
            parserPath = argv[++i];
        else if (strcmp(argv[i], "--server-path") == 0 && i + 1 < argc)
            serverPath = argv[++i];
    }

    if (Logger_Initiate("logs/watchdog.log", LOG_LEVEL_DEBUG) != 0)
    {
        fprintf(stderr, "Failed to initialize logger\n");
        return 1;
    }

    LOG_INFO("Watchdog: Starting with fetcher=%s, parser=%s, server=%s", fetcherPath, parserPath, serverPath);

    int result = Watchdog_Run(fetcherPath, parserPath, serverPath);

    Logger_Shutdown();
    return result;
}
