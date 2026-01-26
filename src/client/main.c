

#include "TCPClient.h"

int main()
{
    TCPClient client;

    TCPClient_Initiate(&client, -1);

    TCPClient_Connect(&client, "localhost", "8080");

    // TCPClient_Disconnect(&client);
    while(1) {};

    return 0;
}