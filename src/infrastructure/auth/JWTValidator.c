#define _POSIX_C_SOURCE 200809L

#include "JWTValidator.h"
#include "Logger.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include <mbedtls/md.h>
#include <mbedtls/base64.h>
#include <mbedtls/constant_time.h>

// ---------------------------------------------------------------------------
// Internal: base64url → standard base64, then decode with mbedtls_base64_decode.
// Returns decoded byte count on success, -1 on error.
// ---------------------------------------------------------------------------
static int base64url_decode(const char *src, size_t srcLen,
                             unsigned char *dst, size_t dstBufLen)
{
    // Pad to multiple of 4
    size_t padded = srcLen + ((4 - srcLen % 4) % 4);
    if (padded + 1 > dstBufLen)
        return -1;

    char *buf = malloc(padded + 1);
    if (!buf)
        return -1;

    memcpy(buf, src, srcLen);

    // base64url alphabet → standard base64 alphabet
    for (size_t i = 0; i < srcLen; i++)
    {
        if (buf[i] == '-') buf[i] = '+';
        else if (buf[i] == '_') buf[i] = '/';
    }
    // Fill padding
    for (size_t i = srcLen; i < padded; i++)
        buf[i] = '=';
    buf[padded] = '\0';

    size_t olen = 0;
    int ret = mbedtls_base64_decode(dst, dstBufLen, &olen,
                                    (const unsigned char *)buf, padded);
    free(buf);

    if (ret != 0)
        return -1;

    return (int)olen;
}

// ---------------------------------------------------------------------------
// Internal: extract a string value from a minimal JSON object.
// Finds the first occurrence of "field":"<value>" and copies <value> into out.
// Returns 0 on success, -1 if not found or too long.
// ---------------------------------------------------------------------------
static int json_get_string(const char *json, const char *field,
                            char *out, size_t outSize)
{
    // Build search needle: "field":"
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":\"", field);

    const char *p = strstr(json, needle);
    if (!p)
        return -1;

    p += strlen(needle);
    const char *end = strchr(p, '"');
    if (!end)
        return -1;

    size_t len = (size_t)(end - p);
    if (len >= outSize)
        return -1;

    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

// ---------------------------------------------------------------------------
// Internal: extract a numeric (long) value from a minimal JSON object.
// Finds the first occurrence of "field":<number>.
// Returns 0 on success, -1 if not found or not a valid number.
// ---------------------------------------------------------------------------
static int json_get_long(const char *json, const char *field, long *out)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":", field);

    const char *p = strstr(json, needle);
    if (!p)
        return -1;

    p += strlen(needle);

    char *endptr;
    long val = strtol(p, &endptr, 10);
    if (endptr == p)
        return -1;

    *out = val;
    return 0;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
int JWT_Validate(const char *token, JWTClaims *claims)
{
    if (!token || !claims)
        return -1;

    // Read secret from environment — never hardcode.
    const char *secret = getenv("GRIDGUARD_JWT_SECRET");
    if (!secret || secret[0] == '\0')
    {
        LOG_ERROR("JWTValidator: GRIDGUARD_JWT_SECRET not set");
        return -1;
    }

    // -----------------------------------------------------------------------
    // 1. Split "header.payload.signature" at '.' — expect exactly 3 parts.
    // -----------------------------------------------------------------------
    // Work on a copy so we can null-terminate parts.
    size_t tokenLen = strlen(token);
    char *copy = malloc(tokenLen + 1);
    if (!copy)
        return -1;
    memcpy(copy, token, tokenLen + 1);

    // Compute signing length before mutating copy — signingInput is "header.payload",
    // i.e. everything before the last '.' in the original token.
    const char *lastDot = strrchr(token, '.');
    if (!lastDot || lastDot == token) { free(copy); return -1; }
    const char *signingInput = token;
    size_t signingLen = (size_t)(lastDot - token);

    char *dot1 = strchr(copy, '.');
    if (!dot1) { free(copy); return -1; }
    *dot1 = '\0';
    char *headerB64  = copy;
    char *rest       = dot1 + 1;

    char *dot2 = strchr(rest, '.');
    if (!dot2) { free(copy); return -1; }
    *dot2 = '\0';
    char *payloadB64 = rest;
    char *sigB64     = dot2 + 1;

    // -----------------------------------------------------------------------
    // 2. Decode header JSON and verify algorithm is HS256.
    // -----------------------------------------------------------------------
    size_t headerB64Len = strlen(headerB64);
    unsigned char headerJson[512];
    int headerLen = base64url_decode(headerB64, headerB64Len,
                                     headerJson, sizeof(headerJson) - 1);
    if (headerLen < 0)
    {
        LOG_WARNING("JWTValidator: Failed to decode header");
        free(copy);
        return -1;
    }
    headerJson[headerLen] = '\0';

    char alg[16] = {0};
    if (json_get_string((char *)headerJson, "alg", alg, sizeof(alg)) != 0
        || strcmp(alg, "HS256") != 0)
    {
        LOG_WARNING("JWTValidator: Unsupported algorithm '%s'", alg);
        free(copy);
        return -1;
    }

    // -----------------------------------------------------------------------
    // 3. Decode payload JSON.
    // -----------------------------------------------------------------------
    size_t payloadB64Len = strlen(payloadB64);
    unsigned char payloadJson[1024];
    int payloadLen = base64url_decode(payloadB64, payloadB64Len,
                                      payloadJson, sizeof(payloadJson) - 1);
    if (payloadLen < 0)
    {
        LOG_WARNING("JWTValidator: Failed to decode payload");
        free(copy);
        return -1;
    }
    payloadJson[payloadLen] = '\0';

    // -----------------------------------------------------------------------
    // 4. Verify HMAC-SHA256 signature.
    //    Compute HMAC over "header.payload" (raw bytes from original token).
    // -----------------------------------------------------------------------
    unsigned char computedSig[32];

    const mbedtls_md_info_t *mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (mbedtls_md_hmac(mdInfo,
                        (const unsigned char *)secret, strlen(secret),
                        (const unsigned char *)signingInput, signingLen,
                        computedSig) != 0)
    {
        LOG_ERROR("JWTValidator: HMAC computation failed");
        free(copy);
        return -1;
    }

    // Decode signature from token
    size_t sigB64Len = strlen(sigB64);
    unsigned char tokenSig[64];
    int tokenSigLen = base64url_decode(sigB64, sigB64Len,
                                       tokenSig, sizeof(tokenSig));
    free(copy); // done with split copy

    if (tokenSigLen < 0 || tokenSigLen != 32)
    {
        LOG_WARNING("JWTValidator: Signature length mismatch");
        return -1;
    }

    // Constant-time comparison to resist timing attacks.
    if (mbedtls_ct_memcmp(computedSig, tokenSig, 32) != 0)
    {
        LOG_WARNING("JWTValidator: Signature verification failed");
        return -1;
    }

    // -----------------------------------------------------------------------
    // 5. Extract and validate claims.
    // -----------------------------------------------------------------------
    long exp = 0;
    if (json_get_long((char *)payloadJson, "exp", &exp) != 0)
    {
        LOG_WARNING("JWTValidator: Missing 'exp' claim");
        return -1;
    }

    if (exp <= (long)time(NULL))
    {
        LOG_WARNING("JWTValidator: Token has expired (exp=%ld)", exp);
        return -1;
    }

    char sub[JWT_SUBJECT_MAX];
    if (json_get_string((char *)payloadJson, "sub", sub, sizeof(sub)) != 0)
    {
        LOG_WARNING("JWTValidator: Missing 'sub' claim");
        return -1;
    }

    // -----------------------------------------------------------------------
    // 6. Populate output.
    // -----------------------------------------------------------------------
    strncpy(claims->subject, sub, JWT_SUBJECT_MAX - 1);
    claims->subject[JWT_SUBJECT_MAX - 1] = '\0';
    claims->expiresAt = exp;

    LOG_INFO("JWTValidator: Token valid — sub=%s exp=%ld", claims->subject, claims->expiresAt);
    return 0;
}
