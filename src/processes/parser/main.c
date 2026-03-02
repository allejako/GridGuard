#include "parser.h"
#include "Logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static ParserProcess g_process;

static void signal_handler(int sig)
{
    LOG_INFO("parser_main: Received signal %d, shutting down", sig);
    g_process.isRunning = false;
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s <fifo_path> <socket_path>\n", argv[0]);
        fprintf(stderr, "Example: %s /tmp/gridguard_fetch_to_parse.fifo /tmp/gridguard_parse_to_compute.sock\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *fifoPath = argv[1];
    const char *socketPath = argv[2];

    if (Logger_Initiate("logs/parser.log", LOG_LEVEL_DEBUG) != 0)
    {
        fprintf(stderr, "Warning: Logger init failed, continuing anyway\n");
    }

    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    LOG_INFO("parser_main: Starting Parser process (PID %d)", getpid());

    if (ParserProcess_Initiate(&g_process, fifoPath, socketPath) != 0)
    {
        LOG_FATAL("parser_main: Failed to initiate ParserProcess");
        Logger_Shutdown();
        return EXIT_FAILURE;
    }

    int result = ParserProcess_Run(&g_process);

    ParserProcess_Shutdown(&g_process);
    Logger_Shutdown();

    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
