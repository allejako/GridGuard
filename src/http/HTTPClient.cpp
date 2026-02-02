#include "HTTPClient.hpp"
#include "TCPClient.hpp"
#include "Logger.h"
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <errno.h>

#define BUFFER_SIZE 8192

HTTPClient::HTTPClient()
    : statusCode(0), body("")
{
}

HTTPClient::~HTTPClient()
{
}

int HTTPClient::get(const std::string &host, const std::string &port, const std::string &path)
{
    // Reset previous data
    body.clear();
    statusCode = 0;

    // Create TCP connection
    TCPClient client;
    if (client.connect(host, port) < 0)
    {
        Logger_Log(LOG_LEVEL_ERROR, __FILE__, __LINE__, "HTTPClient: Failed to connect to %s:%s", host.c_str(), port.c_str());
        return -1;
    }

    // Build HTTP GET request
    std::ostringstream request;
    request << "GET " << path << " HTTP/1.1\r\n";
    request << "Host: " << host << "\r\n";
    request << "Connection: close\r\n";
    request << "User-Agent: GridGuard/1.0\r\n";
    request << "Accept: application/json\r\n";
    request << "\r\n";

    std::string requestStr = request.str();

    // Send request
    int sent = client.write(requestStr.c_str(), requestStr.length());
    if (sent != (int)requestStr.length())
    {
        Logger_Log(LOG_LEVEL_ERROR, __FILE__, __LINE__, "HTTPClient: Failed to send complete request");
        return -2;
    }

    // Read response
    char buffer[BUFFER_SIZE];
    std::string response;

    int retries = 0;
    const int MAX_RETRIES = 100;

    while (retries < MAX_RETRIES)
    {
        int bytesRead = client.read(buffer, BUFFER_SIZE - 1);

        if (bytesRead > 0)
        {
            buffer[bytesRead] = '\0';
            response.append(buffer, bytesRead);
            retries = 0; // Reset retry counter on successful read
        }
        else if (bytesRead == 0)
        {
            // Connection closed by server
            break;
        }
        else
        {
            // Error or would block
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // Wait 10ms and retry
                usleep(10000);
                retries++;
                continue;
            }
            // Other error
            break;
        }
    }

    client.disconnect();

    if (response.empty())
    {
        Logger_Log(LOG_LEVEL_ERROR, __FILE__, __LINE__, "HTTPClient: No response received");
        return -3;
    }

    // Find end of headers
    size_t headerEnd = response.find("\r\n\r\n");
    if (headerEnd == std::string::npos)
    {
        headerEnd = response.find("\n\n");
        if (headerEnd != std::string::npos)
        {
            headerEnd += 2;
        }
        else
        {
            Logger_Log(LOG_LEVEL_ERROR, __FILE__, __LINE__, "HTTPClient: Invalid HTTP response - no header separator");
            return -4;
        }
    }
    else
    {
        headerEnd += 4;
    }

    // Parse status line (e.g., "HTTP/1.1 200 OK")
    size_t lineEnd = response.find("\r\n");
    if (lineEnd == std::string::npos)
        lineEnd = response.find("\n");

    if (lineEnd != std::string::npos)
    {
        std::string statusLine = response.substr(0, lineEnd);
        size_t spacePos = statusLine.find(' ');
        if (spacePos != std::string::npos)
        {
            std::string codeStr = statusLine.substr(spacePos + 1, 3);
            statusCode = std::stoi(codeStr);
        }
    }

    // Extract body
    body = response.substr(headerEnd);

    Logger_Log(LOG_LEVEL_DEBUG, __FILE__, __LINE__, "HTTPClient: GET %s:%s%s -> Status %d, Body length %zu",
               host.c_str(), port.c_str(), path.c_str(), statusCode, body.length());

    return 0;
}
