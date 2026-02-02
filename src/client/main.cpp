#include "TCPClient.hpp"
#include "Logger.h"
#include "config.h"

int main()
{
    TCPClient client;
    client.connect(SERVER_HOST, SERVER_PORT);

    return 0;
}