#define _POSIX_C_SOURCE 200809L

#include "auth/JWTIssuer.h"
#include "sys/Logger.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <mbedtls/md.h>
#include <mbedtls/base64.h>

#define TOKEN_LIFETIME_SECONDS 86400  // 24 hours for demo

// Standard base64 → base64url encoding.
static int base64url_encode(const unsigned char *src, size_t srcLen, char *dst, size_t dstBufLen)
{
    size_t olen = 0;
    int ret = mbedtls_base64_encode((unsigned char *)dst, dstBufLen, &olen, src, srcLen);
    if (ret != 0)
        return -1;

    // Replace standard base64 chars with base64url alphabet
    for (size_t i = 0; i < olen; i++)
    {
        if (dst[i] == '+') dst[i] = '-';
        else if (dst[i] == '/') dst[i] = '_';
    }

    // Remove padding
    while (olen > 0 && dst[olen - 1] == '=')
        olen--;
    dst[olen] = '\0';

    return (int)olen;
}

int JWTIssuer_CreateToken(const char *userId, const char *email, const char *planType, char *tokenOut)
{
    if (!userId || !email || !planType || !tokenOut)
        return -1;

    const char *secret = getenv("GRIDGUARD_JWT_SECRET");
    if (!secret || secret[0] == '\0')
    {
        LOG_ERROR("JWTIssuer: GRIDGUARD_JWT_SECRET not set");
        return -1;
    }

    // Build header JSON
    const char *headerJson = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";
    char headerB64[256];
    int headerB64Len = base64url_encode((const unsigned char *)headerJson, strlen(headerJson), headerB64, sizeof(headerB64));
    if (headerB64Len < 0)
    {
        LOG_ERROR("JWTIssuer: Failed to encode header");
        return -1;
    }

    // Build payload JSON with userId, email, plan_type
    time_t now = time(NULL);
    time_t expiresAt = now + TOKEN_LIFETIME_SECONDS;

    char payloadJson[512];
    snprintf(payloadJson, sizeof(payloadJson), "{\"sub\":\"%s\",\"email\":\"%s\",\"plan\":\"%s\",\"exp\":%ld}", userId, email, planType, (long)expiresAt);

    char payloadB64[1024];
    int payloadB64Len = base64url_encode((const unsigned char *)payloadJson, strlen(payloadJson), payloadB64, sizeof(payloadB64));
    if (payloadB64Len < 0)
    {
        LOG_ERROR("JWTIssuer: Failed to encode payload");
        return -1;
    }

    // Create signing input "header.payload"
    char signingInput[2048];
    snprintf(signingInput, sizeof(signingInput), "%s.%s", headerB64, payloadB64);

    // Compute HMAC-SHA256 signature
    unsigned char signature[32];
    const mbedtls_md_info_t *mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (mbedtls_md_hmac(mdInfo, (const unsigned char *)secret, strlen(secret), (const unsigned char *)signingInput, strlen(signingInput), signature) != 0)
    {
        LOG_ERROR("JWTIssuer: HMAC computation failed");
        return -1;
    }

    // Encode signature
    char signatureB64[64];
    int signatureB64Len = base64url_encode(signature, 32, signatureB64, sizeof(signatureB64));
    if (signatureB64Len < 0)
    {
        LOG_ERROR("JWTIssuer: Failed to encode signature");
        return -1;
    }

    // Combine "header.payload.signature"
    int written = snprintf(tokenOut, JWT_TOKEN_MAX, "%s.%s.%s", headerB64, payloadB64, signatureB64);
    if (written < 0 || written >= JWT_TOKEN_MAX)
    {
        LOG_ERROR("JWTIssuer: Token too large for buffer");
        return -1;
    }

    LOG_INFO("JWTIssuer: Created token for user=%s plan=%s", userId, planType);
    return 0;
}
