#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <ctype.h>
#include <strings.h>

#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>

#include "HTTPClient.h"

// Decode HTTP/1.1 chunked transfer encoding.
// src/srcLen: raw chunked body.  *out: malloc'd decoded body, caller frees.
// Returns 0 on success, -1 on error.
static int decode_chunked(const char *src, size_t srcLen, char **out, size_t *outLen)
{
    char *buf = malloc(srcLen + 1); // decoded body is always <= encoded size
    if (!buf) return -1;

    size_t      written = 0;
    const char *p       = src;
    const char *end     = src + srcLen;

    while (p < end)
    {
        // Find end of chunk-size line (may have extensions: "800;ext=...\r\n")
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        if (!nl) break;

        unsigned long sz = strtoul(p, NULL, 16);
        p = nl + 1; // advance past '\n'

        if (sz == 0) break; // final zero-length chunk

        // Compare sz against available bytes without pointer arithmetic overflow.
        // p + sz could wrap if sz is near SIZE_MAX on a 32-bit build.
        if (sz > (size_t)(end - p)) { free(buf); return -1; }

        memcpy(buf + written, p, sz);
        written += sz;
        p += sz;

        // Consume the mandatory CRLF that follows each chunk body
        if (p < end && *p == '\r') p++;
        if (p < end && *p == '\n') p++;
    }

    buf[written] = '\0';
    *out    = buf;
    *outLen = written;
    return 0;
}

// Case-insensitive substring search (portable, no _GNU_SOURCE needed).
static const char *str_istr(const char *haystack, const char *needle)
{
    size_t nlen = strlen(needle);
    for (; *haystack; haystack++)
    {
        if (strncasecmp(haystack, needle, nlen) == 0)
            return haystack;
    }
    return NULL;
}

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
    mbedtls_x509_crt_init(&client->cacert);

    if (mbedtls_ctr_drbg_seed(&client->ctrDrbg, mbedtls_entropy_func, &client->entropy, NULL, 0) != 0)
    {
        mbedtls_x509_crt_free(&client->cacert);
        mbedtls_ctr_drbg_free(&client->ctrDrbg);
        mbedtls_entropy_free(&client->entropy);
        mbedtls_ssl_config_free(&client->sslConf);
        return -1;
    }

    // Load system CA certificates for proper TLS verification
    // Try common Linux CA certificate locations
    int ca_loaded = 0;
    const char *ca_paths[] = {
        "/etc/ssl/certs/ca-certificates.crt",  // Debian/Ubuntu
        "/etc/pki/tls/certs/ca-bundle.crt",    // RHEL/Fedora
        "/etc/ssl/ca-bundle.pem",              // OpenSUSE
        "/etc/ssl/cert.pem",                   // Alpine Linux
        NULL
    };

    for (int i = 0; ca_paths[i] != NULL; i++)
    {
        if (mbedtls_x509_crt_parse_file(&client->cacert, ca_paths[i]) == 0)
        {
            ca_loaded = 1;
            break;
        }
    }

    if (!ca_loaded)
    {
        // Fallback: try loading from directory
        if (mbedtls_x509_crt_parse_path(&client->cacert, "/etc/ssl/certs") != 0)
        {
            mbedtls_x509_crt_free(&client->cacert);
            mbedtls_ctr_drbg_free(&client->ctrDrbg);
            mbedtls_entropy_free(&client->entropy);
            mbedtls_ssl_config_free(&client->sslConf);
            return -1;
        }
    }

    if (mbedtls_ssl_config_defaults(&client->sslConf,
                                    MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0)
    {
        mbedtls_x509_crt_free(&client->cacert);
        mbedtls_ctr_drbg_free(&client->ctrDrbg);
        mbedtls_entropy_free(&client->entropy);
        mbedtls_ssl_config_free(&client->sslConf);
        return -1;
    }

    // Enable proper certificate verification (prevents MITM attacks)
    mbedtls_ssl_conf_authmode(&client->sslConf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&client->sslConf, &client->cacert, NULL);
    mbedtls_ssl_conf_rng(&client->sslConf, mbedtls_ctr_drbg_random, &client->ctrDrbg);

    client->initialized = true;
    return 0;
}

void HTTPClient_Shutdown(HTTPClient *client)
{
    if (!client || !client->initialized)
        return;

    mbedtls_x509_crt_free(&client->cacert);
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

    // Hard cap on response size to prevent unbounded memory growth.
    // 4 MB is generous for any API response this server consumes.
#define HTTP_CLIENT_MAX_RESPONSE_SIZE (4u * 1024u * 1024u)

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
                if (capacity >= HTTP_CLIENT_MAX_RESPONSE_SIZE)
                {
                    free(buf);
                    mbedtls_ssl_free(&ssl);
                    close(fd);
                    return -1;
                }
                size_t newCapacity = capacity * 2;
                if (newCapacity > HTTP_CLIENT_MAX_RESPONSE_SIZE)
                    newCapacity = HTTP_CLIENT_MAX_RESPONSE_SIZE;
                char *tmp = realloc(buf, newCapacity);
                if (!tmp)
                {
                    free(buf);
                    mbedtls_ssl_free(&ssl);
                    close(fd);
                    return -1;
                }
                buf      = tmp;
                capacity = newCapacity;
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

    // Detect chunked encoding by looking in the headers section only
    size_t headerLen = (size_t)(bodyStart - buf);
    int    chunked   = 0;
    {
        char *hdr = malloc(headerLen + 1);
        if (hdr)
        {
            memcpy(hdr, buf, headerLen);
            hdr[headerLen] = '\0';
            chunked = str_istr(hdr, "transfer-encoding: chunked") != NULL;
            free(hdr);
        }
    }

    bodyStart += 4; // skip past \r\n\r\n
    size_t rawBodyLen = total - (size_t)(bodyStart - buf);

    char  *body;
    size_t bodyLen;

    if (chunked)
    {
        if (decode_chunked(bodyStart, rawBodyLen, &body, &bodyLen) != 0)
        {
            free(buf);
            return -1;
        }
    }
    else
    {
        body = malloc(rawBodyLen + 1);
        if (!body) { free(buf); return -1; }
        memcpy(body, bodyStart, rawBodyLen);
        body[rawBodyLen] = '\0';
        bodyLen = rawBodyLen;
    }

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
