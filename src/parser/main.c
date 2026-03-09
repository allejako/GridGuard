#include "parser/Parser.h"
#include "sys/Logger.h"
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

static ParserProcess g_process;

static void signal_handler(int sig)
{
    LOG_INFO("parser_main: Received signal %d, shutting down", sig);
    g_process.isRunning = false;
}

int main(int argc __attribute__((unused)), char *argv[])
{
    const char *fifoPath = argv[1];
    const char *socketPath = argv[2];

    Logger_Initiate("logs/parser.log", LOG_LEVEL_DEBUG);

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
