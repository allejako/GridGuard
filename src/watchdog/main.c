#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "watchdog/Watchdog.h"
#include "sys/Logger.h"

#define DEFAULT_FETCHER_PATH "bin/GridGuard-fetcher"
#define DEFAULT_PARSER_PATH  "bin/GridGuard-parser"
#define DEFAULT_SERVER_PATH  "bin/GridGuard-server"

static void print_usage(const char *progname)
{
    fprintf(stderr, "Usage: %s [OPTIONS]\n", progname);
    fprintf(stderr, "  --fetcher-path PATH  Path to GridGuard-fetcher binary (default: %s)\n", DEFAULT_FETCHER_PATH);
    fprintf(stderr, "  --parser-path PATH   Path to GridGuard-parser binary (default: %s)\n", DEFAULT_PARSER_PATH);
    fprintf(stderr, "  --server-path PATH   Path to GridGuard-server binary (default: %s)\n", DEFAULT_SERVER_PATH);
}

int main(int argc, char *argv[])
{
    const char *fetcherPath = DEFAULT_FETCHER_PATH;
    const char *parserPath = DEFAULT_PARSER_PATH;
    const char *serverPath = DEFAULT_SERVER_PATH;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--fetcher-path") == 0 && i + 1 < argc)
        {
            fetcherPath = argv[++i];
        }
        else if (strcmp(argv[i], "--parser-path") == 0 && i + 1 < argc)
        {
            parserPath = argv[++i];
        }
        else if (strcmp(argv[i], "--server-path") == 0 && i + 1 < argc)
        {
            serverPath = argv[++i];
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

    if (Logger_Initiate("logs/watchdog.log", LOG_LEVEL_DEBUG) != 0)
    {
        fprintf(stderr, "Failed to initialize logger\n");
        return EXIT_FAILURE;
    }

    LOG_INFO("Watchdog: Starting with fetcher=%s, parser=%s, server=%s",
             fetcherPath, parserPath, serverPath);

    int result = Watchdog_Run(fetcherPath, parserPath, serverPath);

    Logger_Shutdown();
    return result;
}
