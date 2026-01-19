#define _POSIX_C_SOURCE 200809L

#include "TCPClient.h"

int TCPClient_Initiate(TCPClient *_Client, int fd)
{
    _Client->fd = fd;
    return 0;
}

int TCPClient_Connect(TCPClient *_Client, const char *_Host, const char *_Port)
{
    if (_Client->fd >= 0)
        return -1;

    struct addrinfo hints = {0};
    struct addrinfo *res = NULL;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(_Host, _Port, &hints, &res) != 0)
        return -1;

    int fd = -1;
    for (struct addrinfo *rp = res; rp; rp = rp->ai_next)
    {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (fd < 0)
            continue;

        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;

        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    if (fd < 0)
        return -1;

    _Client->fd = fd;
    return 0;
}

int TCPClient_Write(TCPClient *_Client, const uint8_t *_Buffer, int _Length)
{
    return send(_Client->fd, _Buffer, _Length, MSG_NOSIGNAL); // Non-blocking
}

// Returns number of bytes received
int TCPClient_Read(TCPClient *_Client, uint8_t *_Buffer, int _Length)
{
    return recv(_Client->fd, _Buffer, _Length, MSG_DONTWAIT); // Non-blocking
}

void TCPClient_Disconnect(TCPClient *_Client)
{
    if (_Client->fd >= 0)
        close(_Client->fd);
    printf("Client with fd %d disconnected\n", _Client->fd);
    _Client->fd = -1;
}

void TCPClient_Dispose(TCPClient *_Client)
{
    TCPClient_Disconnect(_Client);
}