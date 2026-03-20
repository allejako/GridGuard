#ifndef _TCP_SERVER_H_
#define _TCP_SERVER_H_

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#define MAX_CLIENTS 10

typedef struct
{
    int listenFd;
} TCPServer;

int  TCPServer_Initiate(TCPServer *server, const char *port);
int  TCPServer_Accept(TCPServer *server);
void TCPServer_Shutdown(TCPServer *server);

#endif // _TCP_SERVER_H_
