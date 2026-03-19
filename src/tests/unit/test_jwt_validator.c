#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "auth/JWTValidator.h"
#include "sys/Logger.h"

// ---------------------------------------------------------------------------
// Minimal test framework
// ---------------------------------------------------------------------------
static int g_tests_run    = 0;
static int g_tests_passed = 0;

#define ASSERT(desc, cond) do {                              \
    g_tests_run++;                                           \
    if (cond) {                                              \
        printf("  [PASS] %s\n", (desc));                     \
        g_tests_passed++;                                    \
    } else {                                                 \
        printf("  [FAIL] %s  (line %d)\n", (desc), __LINE__);\
    }                                                        \
} while (0)

static void print_section(const char *title)
{
    printf("\n=== %s ===\n", title);
}

// ---------------------------------------------------------------------------
// Pre-computed test tokens (secret = "gridguard-test-secret")
//
// header: {"alg":"HS256","typ":"JWT"}
//
// VALID:   sub=test_user, exp=1893456000 (2030-01-01)
// EXPIRED: sub=test_user, exp=1577836800 (2020-01-01)
// WRONG:   sub=test_user, exp=1893456000, signed with "wrong-secret"
// BAD_ALG: sub=test_user, exp=1893456000, alg=RS256
// ---------------------------------------------------------------------------
#define SECRET "gridguard-test-secret"

#define TOKEN_VALID \
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9" \
    ".eyJzdWIiOiJ0ZXN0X3VzZXIiLCJleHAiOjE4OTM0NTYwMDB9" \
    ".d33GazykNsOCuOyy545_484DACV1vEd3owJr-dvL-1c"

#define TOKEN_EXPIRED \
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9" \
    ".eyJzdWIiOiJ0ZXN0X3VzZXIiLCJleHAiOjE1Nzc4MzY4MDB9" \
    ".fYmsVL_nBn-86dXUj-LcnAyqrb1446bKiAl-4N5VwsA"

#define TOKEN_WRONG_SECRET \
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9" \
    ".eyJzdWIiOiJ0ZXN0X3VzZXIiLCJleHAiOjE4OTM0NTYwMDB9" \
    ".VF3-GCQaXy___ilfEv-VAEfOdf4UpNFeNiIyVGgY9Mw"

#define TOKEN_BAD_ALG \
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9" \
    ".eyJzdWIiOiJ0ZXN0X3VzZXIiLCJleHAiOjE4OTM0NTYwMDB9" \
    ".SHRbCoea1X34fgSo38HsRZI7I5cWWQjS8lDOq4E7GP8"

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_valid_token(void)
{
    print_section("Giltig JWT");
    setenv("GRIDGUARD_JWT_SECRET", SECRET, 1);

    JWTClaims claims;
    int result = JWT_Validate(TOKEN_VALID, &claims);

    ASSERT("JWT_Validate returnerar 0 vid giltig token",   result == 0);
    ASSERT("claims.subject == \"test_user\"",               strcmp(claims.subject, "test_user") == 0);
    ASSERT("claims.expiresAt == 1893456000 (2030-01-01)", claims.expiresAt == 1893456000L);
}

static void test_expired_token(void)
{
    print_section("Expired JWT");
    setenv("GRIDGUARD_JWT_SECRET", SECRET, 1);

    JWTClaims claims;
    int result = JWT_Validate(TOKEN_EXPIRED, &claims);

    ASSERT("JWT_Validate returns -1 for expired token", result == -1);
}

static void test_wrong_secret(void)
{
    print_section("Wrong secret key");
    setenv("GRIDGUARD_JWT_SECRET", SECRET, 1);

    JWTClaims claims;
    int result = JWT_Validate(TOKEN_WRONG_SECRET, &claims);

    ASSERT("JWT_Validate returns -1 for wrong signature", result == -1);
}

static void test_missing_secret_env(void)
{
    print_section("Missing GRIDGUARD_JWT_SECRET");
    unsetenv("GRIDGUARD_JWT_SECRET");

    JWTClaims claims;
    int result = JWT_Validate(TOKEN_VALID, &claims);

    ASSERT("JWT_Validate returns -1 when env var is missing", result == -1);

    // Restore for subsequent tests
    setenv("GRIDGUARD_JWT_SECRET", SECRET, 1);
}

static void test_wrong_algorithm(void)
{
    print_section("Algoritm != HS256");
    setenv("GRIDGUARD_JWT_SECRET", SECRET, 1);

    JWTClaims claims;
    int result = JWT_Validate(TOKEN_BAD_ALG, &claims);

    ASSERT("JWT_Validate returnerar -1 vid RS256-token", result == -1);
}

static void test_malformed_token(void)
{
    print_section("Malformed tokens");
    setenv("GRIDGUARD_JWT_SECRET", SECRET, 1);

    JWTClaims claims;

    ASSERT("Empty string returns -1",
           JWT_Validate("", &claims) == -1);
    ASSERT("No dots returns -1",
           JWT_Validate("nodots", &claims) == -1);
    ASSERT("Only one dot returns -1",
           JWT_Validate("header.payload", &claims) == -1);
    ASSERT("NULL token returns -1",
           JWT_Validate(NULL, &claims) == -1);
    ASSERT("NULL claims returns -1",
           JWT_Validate(TOKEN_VALID, NULL) == -1);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(void)
{
    // Silence the logger — we only care about test output
    Logger_Initiate("logs/test_jwt.log", LOG_LEVEL_WARNING);

    printf("=========================================\n");
    printf("       JWTValidator — Unit Tests\n");
    printf("=========================================\n");

    test_valid_token();
    test_expired_token();
    test_wrong_secret();
    test_missing_secret_env();
    test_wrong_algorithm();
    test_malformed_token();

    printf("\n=========================================\n");
    printf("Results: %d/%d tests passed\n", g_tests_passed, g_tests_run);
    printf("=========================================\n");

    Logger_Shutdown();
    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
