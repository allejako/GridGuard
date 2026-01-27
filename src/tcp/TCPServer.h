
#ifndef __TCPServer_h_
#define __TCPServer_h_

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
	int listen_fd;

} TCPServer;


int TCPServer_Initiate(TCPServer* server, const char* port);

int TCPServer_Accept(TCPServer* server);

void TCPServer_Dispose(TCPServer* server);

#endif //__TCPServer_h_
