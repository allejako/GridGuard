#include <stdio.h>
#include <stdlib.h>

#include "Server.h"
#include "Logger.h"

int main()
{
    // Initialize logger
    if (Logger_Initiate("../../logs", LOG_LEVEL_DEBUG) != 0)
    {
        fprintf(stderr, "Failed to initialize logger\n");
        return EXIT_FAILURE;
    }

    // Create and initialize server
    Server server;
    if (Server_Initiate(&server) != 0)
    {
        LOG_FATAL("Failed to initialize server");
        Logger_Shutdown();
        return EXIT_FAILURE;
    }

    // Run server main loop
    Server_Run(&server);

    // Cleanup
    Server_Shutdown(&server);
    Logger_Shutdown();

    LOG_INFO("Server exited cleanly");
    return EXIT_SUCCESS;
}
