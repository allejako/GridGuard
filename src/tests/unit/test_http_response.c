#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "net/HTTPResponse.h"

// ---------------------------------------------------------------------------
// Minimal test framework
// ---------------------------------------------------------------------------
static int g_tests_run    = 0;
static int g_tests_passed = 0;

#define ASSERT(desc, cond) do {                               \
    g_tests_run++;                                            \
    if (cond) {                                               \
        printf("  [PASS] %s\n", (desc));                      \
        g_tests_passed++;                                     \
    } else {                                                  \
        printf("  [FAIL] %s  (line %d)\n", (desc), __LINE__); \
    }                                                         \
} while (0)

static void print_section(const char *title)
{
    printf("\n=== %s ===\n", title);
}


// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_200_ok_json(void)
{
    print_section("200 OK — JSON response");

    char buf[2048] = {0};
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) { g_tests_run++; return; }

    HTTPResponse_SendJson(fds[0], "{\"status\":\"ok\"}");
    close(fds[0]);

    ssize_t n = read(fds[1], buf, sizeof(buf) - 1);
    close(fds[1]);
    buf[n > 0 ? n : 0] = '\0';

    ASSERT("Response contains 'HTTP/1.1 200'",        strstr(buf, "HTTP/1.1 200") != NULL);
    ASSERT("Content-Type is application/json",        strstr(buf, "application/json") != NULL);
    ASSERT("Body contains JSON data",                 strstr(buf, "{\"status\":\"ok\"}") != NULL);
}

static void test_401_unauthorized(void)
{
    print_section("401 Unauthorized");

    char buf[2048] = {0};
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) { g_tests_run++; return; }

    HTTPResponse_SendError(fds[0], HTTP_STATUS_401_UNAUTHORIZED, "Unauthorized");
    close(fds[0]);

    ssize_t n = read(fds[1], buf, sizeof(buf) - 1);
    close(fds[1]);
    buf[n > 0 ? n : 0] = '\0';

    ASSERT("Response contains 'HTTP/1.1 401'",     strstr(buf, "HTTP/1.1 401") != NULL);
    ASSERT("Body contains 'Unauthorized'",         strstr(buf, "Unauthorized") != NULL);
    ASSERT("Content-Type is application/json",     strstr(buf, "application/json") != NULL);
}

static void test_404_not_found(void)
{
    print_section("404 Not Found");

    char buf[2048] = {0};
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) { g_tests_run++; return; }

    HTTPResponse_SendError(fds[0], HTTP_STATUS_404_NOT_FOUND, "Not found");
    close(fds[0]);

    ssize_t n = read(fds[1], buf, sizeof(buf) - 1);
    close(fds[1]);
    buf[n > 0 ? n : 0] = '\0';

    ASSERT("Response contains 'HTTP/1.1 404'", strstr(buf, "HTTP/1.1 404") != NULL);
    ASSERT("Body contains 'Not found'",        strstr(buf, "Not found") != NULL);
}

static void test_500_internal_error(void)
{
    print_section("500 Internal Server Error");

    char buf[2048] = {0};
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) { g_tests_run++; return; }

    HTTPResponse_SendError(fds[0], HTTP_STATUS_500_INTERNAL_SERVER_ERROR, "Internal error");
    close(fds[0]);

    ssize_t n = read(fds[1], buf, sizeof(buf) - 1);
    close(fds[1]);
    buf[n > 0 ? n : 0] = '\0';

    ASSERT("Response contains 'HTTP/1.1 500'",    strstr(buf, "HTTP/1.1 500") != NULL);
    ASSERT("Body contains 'Internal error'",      strstr(buf, "Internal error") != NULL);
}

static void test_response_has_content_length(void)
{
    print_section("Content-Length header present in all responses");

    char buf[2048] = {0};
    int fds[2];

    // Test with 200
    socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    HTTPResponse_SendJson(fds[0], "{\"key\":\"value\"}");
    close(fds[0]);
    ssize_t n = read(fds[1], buf, sizeof(buf) - 1);
    close(fds[1]);
    buf[n > 0 ? n : 0] = '\0';

    ASSERT("200 response has Content-Length", strstr(buf, "Content-Length:") != NULL);

    // Test with 401
    memset(buf, 0, sizeof(buf));
    socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    HTTPResponse_SendError(fds[0], HTTP_STATUS_401_UNAUTHORIZED, "Unauthorized");
    close(fds[0]);
    n = read(fds[1], buf, sizeof(buf) - 1);
    close(fds[1]);
    buf[n > 0 ? n : 0] = '\0';

    ASSERT("401 response has Content-Length", strstr(buf, "Content-Length:") != NULL);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(void)
{
    printf("=========================================\n");
    printf("       HTTPResponse — Unit Tests\n");
    printf("=========================================\n");

    test_200_ok_json();
    test_401_unauthorized();
    test_404_not_found();
    test_500_internal_error();
    test_response_has_content_length();

    printf("\n=========================================\n");
    printf("Results: %d/%d tests passed\n", g_tests_passed, g_tests_run);
    printf("=========================================\n");

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
