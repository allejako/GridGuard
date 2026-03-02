#include "fetcher.h"
#include "Logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static FetcherProcess g_process;

static void signal_handler(int sig)
{
    LOG_INFO("fetcher_main: Received signal %d, shutting down", sig);
    g_process.isRunning = false;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <fifo_path>\n", argv[0]);
        fprintf(stderr, "Example: %s /tmp/gridguard_fetch_to_parse.fifo\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *fifoPath = argv[1];

    // Logger har redan initierats av main process
    // Men vi kan ha en lokal fallback för debugging
    if (Logger_Initiate("logs/fetcher.log", LOG_LEVEL_DEBUG) != 0)
    {
        fprintf(stderr, "Warning: Logger init failed, continuing anyway\n");
    }

    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    LOG_INFO("fetcher_main: Starting Fetcher process (PID %d)", getpid());

    if (FetcherProcess_Initiate(&g_process, fifoPath) != 0)
    {
        LOG_FATAL("fetcher_main: Failed to initiate FetcherProcess");
        Logger_Shutdown();
        return EXIT_FAILURE;
    }

    // Kör huvudloopen
    int result = FetcherProcess_Run(&g_process);

    FetcherProcess_Shutdown(&g_process);
    Logger_Shutdown();

    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
