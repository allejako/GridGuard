#include "TCPClient.hpp"

TCPClient::TCPClient()
    : fd(-1) {}

TCPClient::~TCPClient()
{
    disconnect();
}

int TCPClient::connect(const std::string &host, const std::string &port)
{
    if (this->fd >= 0)
    {
        LOG_FATAL("FD is unavailable");
        return -1;
    }

    struct addrinfo hints = {};
    struct addrinfo *res = NULL;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0)
        return -1;

    int fd = -1;
    for (addrinfo *rp = res; rp; rp = rp->ai_next)
    {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

        if (fd < 0)
            continue;

        if (::connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;

        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    if (fd < 0)
        return -1;

    this->fd = fd;

    LOG_INFO("Client FD %d connected", fd);
    return 0;
}

int TCPClient::write(const void *buffer, size_t length)
{
    return send(fd, buffer, length, MSG_NOSIGNAL); // Non-blocking
}

// Returns number of bytes received
int TCPClient::read(void *buffer, size_t length)
{
    return recv(fd, buffer, length, MSG_DONTWAIT); // Non-blocking
}

void TCPClient::disconnect()
{
    if (fd >= 0) {
        LOG_INFO("Client FD %d disconnected", fd);
        close(fd);
        fd = -1;
    }
}