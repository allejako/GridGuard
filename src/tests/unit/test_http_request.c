#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "net/HTTPRequest.h"

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
// Helper: writes an HTTP request to a fd and returns
// the read end of a socket pair — simulates a real TCP connection.
// ---------------------------------------------------------------------------
static int write_request_to_socket(const char *raw, int *read_fd_out)
{
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
        return -1;

    // Write the full HTTP string to the write end, then close
    write(fds[1], raw, strlen(raw));
    close(fds[1]);   // signals EOF → HTTPRequest_Parse stops reading

    *read_fd_out = fds[0];
    return 0;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_parse_health_request(void)
{
    print_section("Parse: GET /health");

    const char *raw =
        "GET /health HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "\r\n";

    int fd;
    if (write_request_to_socket(raw, &fd) != 0)
    {
        printf("  [FAIL] socketpair failed\n");
        g_tests_run++;
        return;
    }

    HTTPRequest req;
    int result = HTTPRequest_Parse(fd, &req);
    close(fd);

    ASSERT("HTTPRequest_Parse returns 0",  result == 0);
    ASSERT("method == \"GET\"",            strcmp(req.method, "GET") == 0);
    ASSERT("path == \"/health\"",          strcmp(req.path, "/health") == 0);
    ASSERT("authorization is empty",       req.authorization[0] == '\0');
}

static void test_parse_forecast_with_auth(void)
{
    print_section("Parse: GET /forecast with Authorization header");

    const char *raw =
        "GET /forecast HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.test.sig\r\n"
        "Accept: application/json\r\n"
        "\r\n";

    int fd;
    if (write_request_to_socket(raw, &fd) != 0)
    {
        printf("  [FAIL] socketpair failed\n");
        g_tests_run++;
        return;
    }

    HTTPRequest req;
    int result = HTTPRequest_Parse(fd, &req);
    close(fd);

    ASSERT("HTTPRequest_Parse returns 0", result == 0);
    ASSERT("method == \"GET\"",           strcmp(req.method, "GET") == 0);
    ASSERT("path == \"/forecast\"",       strcmp(req.path, "/forecast") == 0);
    ASSERT("authorization is populated", req.authorization[0] != '\0');
}

static void test_get_bearer_token_valid(void)
{
    print_section("GetBearerToken: valid Bearer header");

    const char *raw =
        "GET /forecast HTTP/1.1\r\n"
        "Authorization: Bearer mytoken123\r\n"
        "\r\n";

    int fd;
    if (write_request_to_socket(raw, &fd) != 0)
    {
        printf("  [FAIL] socketpair failed\n");
        g_tests_run++;
        return;
    }

    HTTPRequest req;
    HTTPRequest_Parse(fd, &req);
    close(fd);

    const char *token = HTTPRequest_GetBearerToken(&req);

    ASSERT("GetBearerToken returns non-NULL",             token != NULL);
    ASSERT("Token == \"mytoken123\"",
           token != NULL && strcmp(token, "mytoken123") == 0);
}

static void test_get_bearer_token_missing(void)
{
    print_section("GetBearerToken: missing Authorization header");

    const char *raw =
        "GET /forecast HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";

    int fd;
    if (write_request_to_socket(raw, &fd) != 0)
    {
        printf("  [FAIL] socketpair failed\n");
        g_tests_run++;
        return;
    }

    HTTPRequest req;
    HTTPRequest_Parse(fd, &req);
    close(fd);

    const char *token = HTTPRequest_GetBearerToken(&req);

    ASSERT("GetBearerToken returns NULL if Authorization is missing", token == NULL);
}

static void test_get_bearer_token_wrong_scheme(void)
{
    print_section("GetBearerToken: Basic scheme (not Bearer)");

    const char *raw =
        "GET /forecast HTTP/1.1\r\n"
        "Authorization: Basic dXNlcjpwYXNz\r\n"
        "\r\n";

    int fd;
    if (write_request_to_socket(raw, &fd) != 0)
    {
        printf("  [FAIL] socketpair failed\n");
        g_tests_run++;
        return;
    }

    HTTPRequest req;
    HTTPRequest_Parse(fd, &req);
    close(fd);

    const char *token = HTTPRequest_GetBearerToken(&req);

    ASSERT("GetBearerToken returns NULL for Basic scheme", token == NULL);
}

static void test_parse_post_with_body(void)
{
    print_section("Parse: POST /forecast with body");

    const char *raw =
        "POST /forecast HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "Content-Length: 13\r\n"
        "\r\n"
        "{\"test\":true}";

    int fd;
    if (write_request_to_socket(raw, &fd) != 0)
    {
        printf("  [FAIL] socketpair failed\n");
        g_tests_run++;
        return;
    }

    HTTPRequest req;
    int result = HTTPRequest_Parse(fd, &req);
    close(fd);

    ASSERT("HTTPRequest_Parse returns 0",  result == 0);
    ASSERT("method == \"POST\"",           strcmp(req.method, "POST") == 0);
    ASSERT("path == \"/forecast\"",        strcmp(req.path, "/forecast") == 0);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(void)
{
    printf("=========================================\n");
    printf("       HTTPRequest — Unit Tests\n");
    printf("=========================================\n");

    test_parse_health_request();
    test_parse_forecast_with_auth();
    test_get_bearer_token_valid();
    test_get_bearer_token_missing();
    test_get_bearer_token_wrong_scheme();
    test_parse_post_with_body();

    printf("\n=========================================\n");
    printf("Results: %d/%d tests passed\n", g_tests_passed, g_tests_run);
    printf("=========================================\n");

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
