

#include "../tcp/tcpclient.h"

int main()
{
    TCPClient client;

    TCPClient_Initiate(&client, -1);

    TCPClient_Connect(&client, "localhost", "8080");

    TCPClient_Disconnect(&client);

    return 0;
}