#include "fetcher/Fetcher.h"
#include "sys/Logger.h"
#include "config/RuntimeConfig.h"
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

static FetcherProcess g_process;

static void HandleSignal(int sig)
{
    LOG_INFO("fetcher_main: Received signal %d, shutting down", sig);
    g_process.isRunning = false;
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <request_fifo_path> <result_fifo_path>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *requestFifoPath = argv[1];
    const char *resultFifoPath = argv[2];

    RuntimeConfig_Load(getenv("GRIDGUARD_CONFIG_PATH"));
    Logger_Initiate("logs/fetcher.log", LOG_LEVEL_DEBUG);

    signal(SIGTERM, HandleSignal);
    signal(SIGINT, HandleSignal);

    LOG_INFO("fetcher_main: Starting Fetcher process (PID %d)", getpid());

    if (FetcherProcess_Initiate(&g_process, requestFifoPath, resultFifoPath) != 0)
    {
        LOG_FATAL("fetcher_main: Failed to initiate FetcherProcess");
        Logger_Shutdown();
        return EXIT_FAILURE;
    }

    int result = FetcherProcess_Run(&g_process);

    FetcherProcess_Shutdown(&g_process);
    Logger_Shutdown();

    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
