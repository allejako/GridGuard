#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stddef.h>
#include <stdbool.h>

#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>

typedef struct {
    char  *body;
    size_t bodyLen;
    int    statusCode;
} HTTPClientResponse;

typedef struct {
    bool                     initialized;
    mbedtls_ssl_config       sslConf;
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context ctrDrbg;
    mbedtls_x509_crt         cacert;
} HTTPClient;

int  HTTPClient_Initiate(HTTPClient *client);
void HTTPClient_Shutdown(HTTPClient *client);

// Blocking HTTPS GET. Returns 0 on success, -1 on failure.
// Caller must free response with HTTPClient_FreeResponse().
int  HTTPClient_Get(HTTPClient *client, const char *url, HTTPClientResponse *response, int timeoutSec);
void HTTPClient_FreeResponse(HTTPClientResponse *response);

#endif // HTTP_CLIENT_H
