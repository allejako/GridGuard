#define _POSIX_C_SOURCE 200809L

#include "net/HTTPRequest.h"
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <stdio.h>

int HTTPRequest_Parse(int fd, HTTPRequest *request)
{
    if (!request || fd < 0)
        return -1;

    memset(request, 0, sizeof(HTTPRequest));

    // Read until \r\n\r\n (end of headers)
    char buffer[HTTP_READ_BUFFER_SIZE];
    int totalRead = 0;

    while (totalRead < HTTP_READ_BUFFER_SIZE - 1)
    {
        int bytesRead = recv(fd,
                             buffer + totalRead,
                             HTTP_READ_BUFFER_SIZE - 1 - totalRead,
                             0);
        if (bytesRead <= 0)
            return -1;

        totalRead += bytesRead;
        buffer[totalRead] = '\0';

        if (strstr(buffer, "\r\n\r\n") != NULL)
            break;
    }

    // Parse first line: "GET /forecast HTTP/1.1"
    if (sscanf(buffer, "%15s %255s", request->method, request->path) != 2)
        return -1;

    // Parse headers line by line
    char *line = strstr(buffer, "\r\n");
    while (line != NULL)
    {
        line += 2; // skip \r\n

        // End of headers
        if (strncmp(line, "\r\n", 2) == 0)
            break;

        if (strncasecmp(line, "Authorization: ", 15) == 0)
        {
            char *valueStart = line + 15;
            char *lineEnd    = strstr(valueStart, "\r\n");
            if (lineEnd != NULL)
            {
                size_t valueLen = (size_t)(lineEnd - valueStart);
                if (valueLen >= HTTP_AUTH_SIZE)
                    valueLen = HTTP_AUTH_SIZE - 1;
                strncpy(request->authorization, valueStart, valueLen);
                request->authorization[valueLen] = '\0';
            }
        }

        line = strstr(line, "\r\n");
    }

    // Body after \r\n\r\n (for future POST support)
    char *bodyStart = strstr(buffer, "\r\n\r\n");
    if (bodyStart != NULL)
    {
        bodyStart += 4;
        int bodyLen = totalRead - (int)(bodyStart - buffer);
        if (bodyLen > 0 && bodyLen < HTTP_BODY_SIZE)
        {
            memcpy(request->body, bodyStart, bodyLen);
            request->bodyLength = bodyLen;
        }
    }

    return 0;
}

const char *HTTPRequest_GetBearerToken(const HTTPRequest *request)
{
    if (!request || request->authorization[0] == '\0')
        return NULL;

    if (strncasecmp(request->authorization, "Bearer ", 7) != 0)
        return NULL;

    return request->authorization + 7;
}
