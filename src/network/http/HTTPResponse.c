// Adapted from LinTechWeather/libs/HTTPServer/HTTPResponse.c
// Uses send(fd) instead of TCPClient_Write — matches GridGuard's server model

#include "HTTPResponse.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

static const char *HTTPResponse_StatusText(HTTPStatusCode code)
{
    switch (code) {
        case HTTP_STATUS_200_OK:                    return "200 OK";
        case HTTP_STATUS_400_BAD_REQUEST:           return "400 Bad Request";
        case HTTP_STATUS_401_UNAUTHORIZED:          return "401 Unauthorized";
        case HTTP_STATUS_404_NOT_FOUND:             return "404 Not Found";
        case HTTP_STATUS_500_INTERNAL_SERVER_ERROR: return "500 Internal Server Error";
        default:                                    return "500 Internal Server Error";
    }
}

int HTTPResponse_Initiate(HTTPResponse *response,
                          HTTPStatusCode statusCode,
                          const char *contentType,
                          const char *body,
                          size_t bodyLength)
{
    if (!response || !contentType)
        return -1;

    response->statusCode  = statusCode;
    response->contentType = contentType;
    response->body        = body;
    response->bodyLength  = bodyLength;

    return 0;
}

int HTTPResponse_Send(const HTTPResponse *response, int fd)
{
    if (!response || fd < 0)
        return -1;

    char header[512];
    int headerLen = snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "\r\n",
        HTTPResponse_StatusText(response->statusCode),
        response->contentType,
        response->bodyLength);

    if (headerLen <= 0 || headerLen >= (int)sizeof(header))
        return -1;

    if (send(fd, header, headerLen, 0) < 0)
        return -1;

    if (response->body != NULL && response->bodyLength > 0)
    {
        if (send(fd, response->body, response->bodyLength, 0) < 0)
            return -1;
    }

    return 0;
}

int HTTPResponse_SendJson(int fd, const char *jsonBody)
{
    if (fd < 0 || !jsonBody)
        return -1;

    HTTPResponse response;
    size_t len = strlen(jsonBody);
    HTTPResponse_Initiate(&response, HTTP_STATUS_200_OK, "application/json", jsonBody, len);

    return HTTPResponse_Send(&response, fd);
}

int HTTPResponse_SendError(int fd, HTTPStatusCode statusCode, const char *errorMessage)
{
    if (fd < 0 || !errorMessage)
        return -1;

    char body[256];
    int bodyLen = snprintf(body, sizeof(body), "{\"error\":\"%s\"}", errorMessage);
    if (bodyLen <= 0 || bodyLen >= (int)sizeof(body))
        return -1;

    HTTPResponse response;
    HTTPResponse_Initiate(&response, statusCode, "application/json", body, bodyLen);

    return HTTPResponse_Send(&response, fd);
}
