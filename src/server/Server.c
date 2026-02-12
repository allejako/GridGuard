#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "Server.h"
#include "SignalHandler.h"
#include "Logger.h"
#include "Config.h"

int Server_Initiate(Server *server)
{
    if (!server)
        return -1;

    LOG_INFO("Server: Initializing...");

    // Initialize signal handler first
    server->isRunning = SignalHandler_Init();

    // Initialize pipeline
    if (Pipeline_Initiate(&server->pipeline) != 0)
    {
        LOG_FATAL("Server: Failed to initialize pipeline");
        return -1;
    }

    // Initialize thread pool with pipeline reference
    if (ThreadPool_Initiate(&server->threadPool, MAX_THREADS, (struct Pipeline *)&server->pipeline) != 0)
    {
        LOG_FATAL("Server: Failed to initialize thread pool");
        Pipeline_Shutdown(&server->pipeline);
        return -1;
    }

    // Initialize TCP server
    if (TCPServer_Initiate(&server->tcpServer, SERVER_PORT) != 0)
    {
        LOG_FATAL("Server: Failed to initialize TCP server on port %s", SERVER_PORT);
        ThreadPool_Shutdown(&server->threadPool);
        Pipeline_Shutdown(&server->pipeline);
        return -1;
    }

    // Register server fd with signal handler
    SignalHandler_SetServerFd(server->tcpServer.listen_fd);

    LOG_INFO("Server: Initialization complete");
    return 0;
}

int Server_Run(Server *server)
{
    if (!server || !server->isRunning)
        return -1;

    LOG_INFO("Server: Starting main loop...");

    while (*server->isRunning)
    {
        int clientSocket = TCPServer_Accept(&server->tcpServer);
        if (clientSocket <= 0)
        {
            if (!*server->isRunning)
                break;
            if (clientSocket < 0)
                LOG_ERROR("Server: Failed to accept connection");
            continue;
        }

        if (ThreadPool_AddClient(&server->threadPool, clientSocket) != 0)
        {
            LOG_ERROR("Server: Thread pool full, rejecting client");
            close(clientSocket);
        }
    }

    LOG_INFO("Server: Main loop exited");
    return 0;
}

void Server_Shutdown(Server *server)
{
    if (!server)
        return;

    LOG_INFO("Server: Shutting down...");

    // Shutdown components in reverse order
    ThreadPool_Shutdown(&server->threadPool);
    Pipeline_Shutdown(&server->pipeline);

    if (server->tcpServer.listen_fd >= 0)
    {
        close(server->tcpServer.listen_fd);
    }

    LOG_INFO("Server: Shutdown complete");
}
