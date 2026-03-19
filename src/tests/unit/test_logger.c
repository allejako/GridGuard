#include <stdio.h>
#include "sys/Logger.h"

int main(void)
{
    // Init logger - writes to both console and file
    if (Logger_Initiate("test.log", LOG_LEVEL_DEBUG) != 0) {
        printf("Failed to init logger\n");
        return 1;
    }

    // Test all log levels
    LOG_DEBUG("This is a debug message");
    LOG_INFO("Server started on port %d", 8080);
    LOG_WARNING("Warning: cache is %d%% full", 85);
    LOG_ERROR("Could not connect to client: %s", "timeout");
    LOG_FATAL("Critical error!");

    // Test changing log level
    printf("\n--- Changing to WARNING level ---\n\n");
    Logger_SetLevel(LOG_LEVEL_WARNING);

    LOG_DEBUG("This should NOT appear");
    LOG_INFO("This should NOT appear either");
    LOG_WARNING("This SHOULD appear");
    LOG_ERROR("This SHOULD also appear");

    Logger_Shutdown();

    printf("\n--- Check test.log for file output ---\n");
    return 0;
}
