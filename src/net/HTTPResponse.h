#ifndef _HTTP_RESPONSE_H_
#define _HTTP_RESPONSE_H_

#include <stddef.h>

// Adapted from LinTechWeather/libs/HTTPServer/HTTPResponse.h
// Uses int fd (send()) instead of TCPClient* — matches GridGuard's server model

typedef enum {
    HTTP_STATUS_200_OK                    = 200,
    HTTP_STATUS_400_BAD_REQUEST           = 400,
    HTTP_STATUS_401_UNAUTHORIZED          = 401,
    HTTP_STATUS_404_NOT_FOUND             = 404,
    HTTP_STATUS_500_INTERNAL_SERVER_ERROR = 500
} HTTPStatusCode;

typedef struct {
    HTTPStatusCode  statusCode;
    const char     *contentType;
    const char     *body;
    size_t          bodyLength;
} HTTPResponse;

int HTTPResponse_Initiate(HTTPResponse *response, HTTPStatusCode statusCode, const char *contentType, const char *body, size_t bodyLength);

// Send full HTTP response over file descriptor.
int HTTPResponse_Send(const HTTPResponse *response, int fd);

// Send 200 OK with JSON body.
int HTTPResponse_SendJson(int fd, const char *jsonBody);

// Send JSON error with given status code.
int HTTPResponse_SendError(int fd, HTTPStatusCode statusCode, const char *errorMessage);

#endif // _HTTP_RESPONSE_H_
