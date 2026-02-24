#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#define HTTP_METHOD_SIZE       16
#define HTTP_PATH_SIZE        256
#define HTTP_AUTH_SIZE       1024
#define HTTP_BODY_SIZE        512
#define HTTP_READ_BUFFER_SIZE 2048

typedef struct {
    char method[HTTP_METHOD_SIZE];      // "GET", "POST"
    char path[HTTP_PATH_SIZE];          // "/forecast", "/weather"
    char authorization[HTTP_AUTH_SIZE]; // Value of Authorization header
    char body[HTTP_BODY_SIZE];          // Request body (POST)
    int  bodyLength;
} HTTPRequest;

// Parse incoming HTTP request from file descriptor.
// Blocks until \r\n\r\n found or error.
// Returns 0 on success, -1 on error.
int HTTPRequest_Parse(int fd, HTTPRequest *request);

// Extract Bearer token from authorization field.
// Returns pointer into request->authorization, or NULL if not a Bearer token.
const char *HTTPRequest_GetBearerToken(const HTTPRequest *request);

#endif // HTTP_REQUEST_H
