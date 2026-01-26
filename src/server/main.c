#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#include "../tcp/tcpserver.h"
#include "../threads/ThreadPool.h"
#include "ClientHandler.h"
#include "SignalHandler.h"

int main()
{
    TCPServer server;
    ThreadPool threadPool;

    volatile sig_atomic_t *serverIsRunning = SignalHandler_Init();

    // Initialize thread pool
    ThreadPool_Initiate(&threadPool, MAX_THREADS);

    // Initialize TCP
    TCPServer_Initiate(&server, "8080");
    SignalHandler_SetServerFd(server.listen_fd);

    while (*serverIsRunning)
    {
        printf("Main: Listening...\n");
        int clientSocket = TCPServer_Accept(&server);
        if (clientSocket < 0)
        {
            if (!*serverIsRunning)
                break;
            perror("accept");
            continue;
        }

        printf("Main: New connection, fd = %d\n", clientSocket);

        // Pass client to thread pool
        if (ThreadPool_AddClient(&threadPool, clientSocket) != 0)
        {
            printf("Main: Thread pool full, rejecting client\n");
            close(clientSocket);
        }
    }

    printf("Server shutting down..\n");

    ThreadPool_Shutdown(&threadPool);
    close(server.listen_fd);

    printf("Server exited cleanly\n");
    return 0;
}
