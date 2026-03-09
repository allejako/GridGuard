#ifndef _SERVER_H_
#define _SERVER_H_

#include <signal.h>
#include "net/TCPServer.h"
#include "sys/ThreadPool.h"
#include "server/GridGuard.h"

typedef struct
{
    TCPServer tcpServer;
    ThreadPool threadPool;
    GridGuard app;
    volatile sig_atomic_t *isRunning;
} Server;

// Initialize all server components
int Server_Initiate(Server *server);

// Run the server main loop (blocking)
int Server_Run(Server *server);

// Shutdown all server components gracefully
void Server_Shutdown(Server *server);

#endif // _SERVER_H_
