#include "fetcher/Fetcher.h"
#include "sys/Logger.h"
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

static FetcherProcess g_process;

static void signal_handler(int sig)
{
    LOG_INFO("fetcher_main: Received signal %d, shutting down", sig);
    g_process.isRunning = false;
}

int main(int argc __attribute__((unused)), char *argv[])
{
    const char *fifoPath = argv[1];

    Logger_Initiate("logs/fetcher.log", LOG_LEVEL_DEBUG);

    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    LOG_INFO("fetcher_main: Starting Fetcher process (PID %d)", getpid());

    if (FetcherProcess_Initiate(&g_process, fifoPath) != 0)
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
