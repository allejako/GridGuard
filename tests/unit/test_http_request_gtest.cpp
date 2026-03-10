#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>

extern "C" {
    #include "net/HTTPRequest.h"
}

class HTTPRequestTest : public ::testing::Test {
protected:
    int clientSocket = -1;
    int serverSocket = -1;

    void SetUp() override {
        // Create a socketpair for testing
        int sockets[2];
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
        clientSocket = sockets[0];
        serverSocket = sockets[1];
    }

    void TearDown() override {
        if (clientSocket >= 0) close(clientSocket);
        if (serverSocket >= 0) close(serverSocket);
    }

    void SendRequest(const char* request) {
        ssize_t sent = send(clientSocket, request, strlen(request), 0);
        ASSERT_GT(sent, 0);
    }
};

TEST_F(HTTPRequestTest, ParseSimpleGETRequest) {
    const char* request =
        "GET /forecast HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";

    SendRequest(request);

    HTTPRequest req;
    int result = HTTPRequest_Parse(serverSocket, &req);

    EXPECT_EQ(result, 0);
    EXPECT_STREQ(req.method, "GET");
    EXPECT_STREQ(req.path, "/forecast");
    EXPECT_EQ(req.authorization[0], '\0');
}

TEST_F(HTTPRequestTest, ParseRequestWithAuthorization) {
    const char* request =
        "GET /weather HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Authorization: Bearer mytoken123\r\n"
        "\r\n";

    SendRequest(request);

    HTTPRequest req;
    int result = HTTPRequest_Parse(serverSocket, &req);

    EXPECT_EQ(result, 0);
    EXPECT_STREQ(req.method, "GET");
    EXPECT_STREQ(req.path, "/weather");
    EXPECT_STREQ(req.authorization, "Bearer mytoken123");
}

TEST_F(HTTPRequestTest, ParsePOSTWithBody) {
    const char* request =
        "POST /data HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 13\r\n"
        "\r\n"
        "{\"key\":\"val\"}";

    SendRequest(request);

    HTTPRequest req;
    int result = HTTPRequest_Parse(serverSocket, &req);

    EXPECT_EQ(result, 0);
    EXPECT_STREQ(req.method, "POST");
    EXPECT_STREQ(req.path, "/data");
    EXPECT_GT(req.bodyLength, 0);
    EXPECT_TRUE(strncmp(req.body, "{\"key\":\"val\"}", 13) == 0);
}

TEST_F(HTTPRequestTest, GetBearerToken) {
    HTTPRequest req;
    memset(&req, 0, sizeof(req));

    // No authorization
    EXPECT_EQ(HTTPRequest_GetBearerToken(&req), nullptr);

    // Bearer token
    strcpy(req.authorization, "Bearer abc123");
    const char* token = HTTPRequest_GetBearerToken(&req);
    ASSERT_NE(token, nullptr);
    EXPECT_STREQ(token, "abc123");

    // Basic auth (not Bearer)
    strcpy(req.authorization, "Basic xyz");
    EXPECT_EQ(HTTPRequest_GetBearerToken(&req), nullptr);

    // Case insensitive
    strcpy(req.authorization, "bearer token456");
    token = HTTPRequest_GetBearerToken(&req);
    ASSERT_NE(token, nullptr);
    EXPECT_STREQ(token, "token456");
}

TEST_F(HTTPRequestTest, ParseInvalidRequest) {
    // Incomplete request (no \r\n\r\n)
    const char* request = "GET /test";

    send(clientSocket, request, strlen(request), 0);
    close(clientSocket);  // Close to trigger EOF
    clientSocket = -1;

    HTTPRequest req;
    int result = HTTPRequest_Parse(serverSocket, &req);

    EXPECT_EQ(result, -1);
}

TEST_F(HTTPRequestTest, ParseNullPointer) {
    HTTPRequest req;
    int result = HTTPRequest_Parse(-1, &req);
    EXPECT_EQ(result, -1);

    result = HTTPRequest_Parse(serverSocket, nullptr);
    EXPECT_EQ(result, -1);
}

TEST_F(HTTPRequestTest, ParseMultipleHeaders) {
    const char* request =
        "GET /api/v1/resource HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "User-Agent: GridGuard-Client\r\n"
        "Authorization: Bearer secrettoken\r\n"
        "Accept: application/json\r\n"
        "\r\n";

    SendRequest(request);

    HTTPRequest req;
    int result = HTTPRequest_Parse(serverSocket, &req);

    EXPECT_EQ(result, 0);
    EXPECT_STREQ(req.method, "GET");
    EXPECT_STREQ(req.path, "/api/v1/resource");
    EXPECT_STREQ(req.authorization, "Bearer secrettoken");
}

TEST_F(HTTPRequestTest, BearerTokenEdgeCases) {
    HTTPRequest req;
    memset(&req, 0, sizeof(req));

    // Empty string after Bearer
    strcpy(req.authorization, "Bearer ");
    const char* token = HTTPRequest_GetBearerToken(&req);
    ASSERT_NE(token, nullptr);
    EXPECT_STREQ(token, "");

    // Just "Bearer" without space
    strcpy(req.authorization, "Bearer");
    token = HTTPRequest_GetBearerToken(&req);
    EXPECT_EQ(token, nullptr);

    // Null pointer
    EXPECT_EQ(HTTPRequest_GetBearerToken(nullptr), nullptr);
}
