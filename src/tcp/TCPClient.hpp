#pragma once

// C++ libraries
#include <iostream>
#include <string>
#include <cstring>

// C libraries
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <fcntl.h>

#include "Config.h"
#include "Logger.h"

class TCPClient
{
public:
    TCPClient();
    ~TCPClient();

    int connect(const std::string &host, const std::string &port);
    int write(const void *buffer, size_t length);
    int read(void *buffer, size_t length);
    void disconnect();

    int fd;
};