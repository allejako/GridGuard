#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>

#include "server/Server.h"
#include "sys/SignalHandler.h"
#include "sys/Logger.h"
#include "domain/Config.h"

int Server_Initiate(Server *server)
{
    if (!server)
        return -1;

    LOG_INFO("Initializing server");

    // Initialize signal handler first
    server->isRunning = SignalHandler_Initiate();

    // Initialize GridGuard application core
    if (GridGuard_Initiate(&server->app) != 0)
    {
        LOG_FATAL("Server: Failed to initialize GridGuard application");
        return -1;
    }

    // Initialize thread pool with GridGuard reference
    if (ThreadPool_Initiate(&server->threadPool, MAX_THREADS, (struct GridGuard *)&server->app) != 0)
    {
        LOG_FATAL("Server: Failed to initialize thread pool");
        GridGuard_Shutdown(&server->app);
        return -1;
    }

    // Initialize TCP server
    if (TCPServer_Initiate(&server->tcpServer, SERVER_PORT) != 0)
    {
        LOG_FATAL("Server: Failed to initialize TCP server on port %s", SERVER_PORT);
        ThreadPool_Shutdown(&server->threadPool);
        GridGuard_Shutdown(&server->app);
        return -1;
    }

    // Register server fd with signal handler
    SignalHandler_SetServerFd(server->tcpServer.listen_fd);

    LOG_INFO("Server ready");
    return 0;
}

int Server_Run(Server *server)
{
    if (!server || !server->isRunning)
        return -1;

    LOG_INFO("Starting main loop");

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

        // Set socket timeout to prevent DoS attacks from slow clients
        struct timeval timeout = { .tv_sec = 30, .tv_usec = 0 };
        setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        if (ThreadPool_AddClient(&server->threadPool, clientSocket) != 0)
        {
            LOG_ERROR("Server: Thread pool full, rejecting client");
            close(clientSocket);
        }
    }

    LOG_INFO("Main loop exited");
    return 0;
}

void Server_Shutdown(Server *server)
{
    if (!server)
        return;

    LOG_INFO("Shutting down");

    // Shutdown components in reverse order
    ThreadPool_Shutdown(&server->threadPool);
    GridGuard_Shutdown(&server->app);

    if (server->tcpServer.listen_fd >= 0)
    {
        close(server->tcpServer.listen_fd);
    }

    LOG_INFO("Shutdown complete");
}
