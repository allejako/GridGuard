#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#include "TCPServer.h"
#include "ThreadPool.h"
#include "ClientHandler.h"
#include "SignalHandler.h"
#include "Logger.h"

int main()
{
    // =============== Initialization ===============

    TCPServer server;
    ThreadPool threadPool;
    volatile sig_atomic_t *serverIsRunning = SignalHandler_Init();

    if (Logger_Initiate("../../logs", LOG_LEVEL_DEBUG) != 0)
    {
        fprintf(stderr, "Failed to initialize logger\n");
    }

    LOG_INFO("Server initializing...");

    if (ThreadPool_Initiate(&threadPool, MAX_THREADS) != 0)
    {
        LOG_FATAL("Failed to initialize thread pool");
        Logger_Shutdown();
        return EXIT_FAILURE;
    }

    if (TCPServer_Initiate(&server, SERVER_PORT) != 0)
    {
        LOG_FATAL("Failed to initialize TCP server on port %s", SERVER_PORT);
        ThreadPool_Shutdown(&threadPool);
        Logger_Shutdown();
        return EXIT_FAILURE;
    }

    SignalHandler_SetServerFd(server.listen_fd);

    // =============== Server loop ===============

    LOG_INFO("Server is up and running...");
    while (*serverIsRunning)
    {
        int clientSocket = TCPServer_Accept(&server);
        if (clientSocket <= 0)
        {
            if (!*serverIsRunning)
                break;
            if (clientSocket < 0)
                LOG_ERROR("Failed to accept connection");
            continue;
        }

        if (ThreadPool_AddClient(&threadPool, clientSocket) != 0)
        {
            LOG_ERROR("Thread pool full, rejecting client");
            close(clientSocket);
        }
    }

    // =============== Shutdown ===============

    LOG_INFO("Server shutting down..");

    ThreadPool_Shutdown(&threadPool);

    if (server.listen_fd >= 0) {
        close(server.listen_fd);
    }

    LOG_INFO("Server exited cleanly");

    return 0;
}
