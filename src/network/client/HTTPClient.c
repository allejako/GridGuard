#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>

#include "HTTPClient.h"

static int parse_url(const char *url,
                     char *host, size_t hostLen,
                     char *port, size_t portLen,
                     char *path, size_t pathLen)
{
    const char *p = url;
    int https = 0;

    if      (strncmp(p, "https://", 8) == 0) { p += 8; https = 1; }
    else if (strncmp(p, "http://",  7) == 0) { p += 7; }
    else return -1;

    const char *slash      = strchr(p, '/');
    const char *colon      = strchr(p, ':');
    size_t      hostEndLen = slash ? (size_t)(slash - p) : strlen(p);

    if (colon && colon < p + hostEndLen)
    {
        size_t hLen = (size_t)(colon - p);
        if (hLen >= hostLen) return -1;
        strncpy(host, p, hLen);
        host[hLen] = '\0';

        const char *portEnd = slash ? slash : p + hostEndLen;
        size_t      pLen    = (size_t)(portEnd - colon - 1);
        if (pLen >= portLen) return -1;
        strncpy(port, colon + 1, pLen);
        port[pLen] = '\0';
    }
    else
    {
        if (hostEndLen >= hostLen) return -1;
        strncpy(host, p, hostEndLen);
        host[hostEndLen] = '\0';
        strncpy(port, https ? "443" : "80", portLen - 1);
    }

    strncpy(path, slash ? slash : "/", pathLen - 1);
    path[pathLen - 1] = '\0';

    return 0;
}

static int tcp_connect(const char *host, const char *port, int timeoutSec)
{
    struct addrinfo hints = {0};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    if (getaddrinfo(host, port, &hints, &res) != 0)
        return -1;

    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next)
    {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;

        struct timeval tv = { .tv_sec = timeoutSec, .tv_usec = 0 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
            break;

        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    return fd;
}

// Bio callbacks — mbedTLS calls these to send/receive raw bytes over our socket.
static int tls_send(void *ctx, const unsigned char *buf, size_t len)
{
    int fd = *(int *)ctx;
    ssize_t n = send(fd, buf, len, 0);
    return (n < 0) ? -1 : (int)n;
}

static int tls_recv(void *ctx, unsigned char *buf, size_t len)
{
    int fd = *(int *)ctx;
    ssize_t n = recv(fd, buf, len, 0);
    return (n < 0) ? -1 : (int)n;
}

int HTTPClient_Initiate(HTTPClient *client)
{
    if (!client)
        return -1;

    mbedtls_ssl_config_init(&client->sslConf);
    mbedtls_entropy_init(&client->entropy);
    mbedtls_ctr_drbg_init(&client->ctrDrbg);

    if (mbedtls_ctr_drbg_seed(&client->ctrDrbg, mbedtls_entropy_func,
                               &client->entropy, NULL, 0) != 0)
    {
        mbedtls_ctr_drbg_free(&client->ctrDrbg);
        mbedtls_entropy_free(&client->entropy);
        mbedtls_ssl_config_free(&client->sslConf);
        return -1;
    }

    if (mbedtls_ssl_config_defaults(&client->sslConf,
                                    MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0)
    {
        mbedtls_ctr_drbg_free(&client->ctrDrbg);
        mbedtls_entropy_free(&client->entropy);
        mbedtls_ssl_config_free(&client->sslConf);
        return -1;
    }

    // Vi verifierar inte servercertifikatet — inga CA-certs behövs på embedded.
    mbedtls_ssl_conf_authmode(&client->sslConf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&client->sslConf, mbedtls_ctr_drbg_random, &client->ctrDrbg);

    client->initialized = true;
    return 0;
}

void HTTPClient_Shutdown(HTTPClient *client)
{
    if (!client || !client->initialized)
        return;

    mbedtls_ssl_config_free(&client->sslConf);
    mbedtls_ctr_drbg_free(&client->ctrDrbg);
    mbedtls_entropy_free(&client->entropy);
    client->initialized = false;
}

int HTTPClient_Get(HTTPClient *client, const char *url, HTTPClientResponse *response, int timeoutSec)
{
    if (!client || !client->initialized || !url || !response)
        return -1;

    char host[256], port[8], path[2048];
    if (parse_url(url, host, sizeof(host), port, sizeof(port), path, sizeof(path)) != 0)
        return -1;

    int fd = tcp_connect(host, port, timeoutSec);
    if (fd < 0)
        return -1;

    mbedtls_ssl_context ssl;
    mbedtls_ssl_init(&ssl);

    if (mbedtls_ssl_setup(&ssl, &client->sslConf) != 0)
    {
        mbedtls_ssl_free(&ssl);
        close(fd);
        return -1;
    }

    mbedtls_ssl_set_hostname(&ssl, host);
    mbedtls_ssl_set_bio(&ssl, &fd, tls_send, tls_recv, NULL);

    int ret;
    while ((ret = mbedtls_ssl_handshake(&ssl)) != 0)
    {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            mbedtls_ssl_free(&ssl);
            close(fd);
            return -1;
        }
    }

    char req[4096];
    int reqLen = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "User-Agent: GridGuard/1.0\r\n"
        "\r\n",
        path, host);

    int written = 0;
    while (written < reqLen)
    {
        ret = mbedtls_ssl_write(&ssl, (unsigned char *)(req + written),
                                (size_t)(reqLen - written));
        if (ret <= 0)
        {
            mbedtls_ssl_free(&ssl);
            close(fd);
            return -1;
        }
        written += ret;
    }

    size_t capacity = 16384;
    size_t total    = 0;
    char  *buf      = malloc(capacity);
    if (!buf)
    {
        mbedtls_ssl_free(&ssl);
        close(fd);
        return -1;
    }

    while (1)
    {
        ret = mbedtls_ssl_read(&ssl, (unsigned char *)(buf + total),
                               capacity - total - 1);
        if (ret > 0)
        {
            total += (size_t)ret;
            if (total + 1 >= capacity)
            {
                capacity *= 2;
                char *tmp = realloc(buf, capacity);
                if (!tmp)
                {
                    free(buf);
                    mbedtls_ssl_free(&ssl);
                    close(fd);
                    return -1;
                }
                buf = tmp;
            }
        }
        else if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
        {
            break;
        }
        else
        {
            free(buf);
            mbedtls_ssl_free(&ssl);
            close(fd);
            return -1;
        }
    }
    buf[total] = '\0';

    mbedtls_ssl_close_notify(&ssl);
    mbedtls_ssl_free(&ssl);
    close(fd);

    int status = 0;
    if (sscanf(buf, "HTTP/%*d.%*d %d", &status) != 1)
    {
        free(buf);
        return -1;
    }

    char *bodyStart = strstr(buf, "\r\n\r\n");
    if (!bodyStart) { free(buf); return -1; }
    bodyStart += 4;

    size_t bodyLen = total - (size_t)(bodyStart - buf);
    char  *body    = malloc(bodyLen + 1);
    if (!body) { free(buf); return -1; }
    memcpy(body, bodyStart, bodyLen);
    body[bodyLen] = '\0';

    free(buf);

    response->body       = body;
    response->bodyLen    = bodyLen;
    response->statusCode = status;

    return 0;
}

void HTTPClient_FreeResponse(HTTPClientResponse *response)
{
    if (!response) return;
    free(response->body);
    response->body       = NULL;
    response->bodyLen    = 0;
    response->statusCode = 0;
}
