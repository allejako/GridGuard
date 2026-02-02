#include "TCPClient.hpp"
#include "Logger.h"
#include "Config.h"

int main()
{
    TCPClient client;
    client.connect(SERVER_HOST, SERVER_PORT);

    return 0;
}