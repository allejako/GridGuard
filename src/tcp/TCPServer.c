#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include "TCPServer.h"


int TCPServer_Initiate(TCPServer* _Server, const char* _Port)
{
	struct addrinfo hints = {0}, *res = NULL;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if (getaddrinfo(NULL, _Port, &hints, &res) != 0)
		return -1;

	int fd = -1;
	for (struct addrinfo *rp = res; rp; rp = rp->ai_next)
	{
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (fd < 0)
			continue;

		int yes = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
		if (bind(fd, rp->ai_addr, rp->ai_addrlen) == 0)
			break;

		close(fd);
		fd = -1;
	}

	freeaddrinfo(res);
	if (fd < 0)
		return -1;

	if (listen(fd, MAX_CLIENTS) < 0)
	{
		close(fd);
		return -1;
	}

	_Server->listen_fd = fd;

	return 0;
}

int TCPServer_Accept(TCPServer* _Server)
{
    int socket_fd = accept(_Server->listen_fd, NULL, NULL);
    if (socket_fd < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0; // No new client

        perror("accept");
        return -1;
    }
    
    printf("TCPServer: accepted connection fd = %d\n", socket_fd);
    
    return socket_fd; // Return the client socket descriptor
}

void TCPServer_Dispose(TCPServer* _Server)
{
	// Close the listening socket
	if (_Server->listen_fd >= 0) {
		close(_Server->listen_fd);
		_Server->listen_fd = -1;
	}
}