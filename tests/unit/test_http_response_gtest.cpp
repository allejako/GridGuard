#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <string>

extern "C" {
    #include "net/HTTPResponse.h"
}

class HTTPResponseTest : public ::testing::Test {
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

    std::string ReceiveResponse() {
        char buffer[2048] = {0};
        ssize_t received = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (received > 0) {
            buffer[received] = '\0';
            return std::string(buffer);
        }
        return "";
    }
};

TEST_F(HTTPResponseTest, InitiateResponse) {
    HTTPResponse response;
    const char* body = "{\"status\":\"ok\"}";

    int result = HTTPResponse_Initiate(&response, HTTP_STATUS_200_OK,
                                        "application/json", body, strlen(body));

    EXPECT_EQ(result, 0);
    EXPECT_EQ(response.statusCode, HTTP_STATUS_200_OK);
    EXPECT_STREQ(response.contentType, "application/json");
    EXPECT_STREQ(response.body, body);
    EXPECT_EQ(response.bodyLength, strlen(body));
}

TEST_F(HTTPResponseTest, InitiateNullPointers) {
    HTTPResponse response;

    // Null response pointer
    int result = HTTPResponse_Initiate(nullptr, HTTP_STATUS_200_OK,
                                        "application/json", "test", 4);
    EXPECT_EQ(result, -1);

    // Null content type
    result = HTTPResponse_Initiate(&response, HTTP_STATUS_200_OK,
                                    nullptr, "test", 4);
    EXPECT_EQ(result, -1);
}

TEST_F(HTTPResponseTest, Send200OK) {
    HTTPResponse response;
    const char* body = "{\"message\":\"success\"}";

    HTTPResponse_Initiate(&response, HTTP_STATUS_200_OK,
                          "application/json", body, strlen(body));

    int result = HTTPResponse_Send(&response, serverSocket);
    EXPECT_EQ(result, 0);

    std::string received = ReceiveResponse();
    EXPECT_TRUE(received.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(received.find("Content-Type: application/json") != std::string::npos);
    EXPECT_TRUE(received.find("Content-Length: 21") != std::string::npos);
    EXPECT_TRUE(received.find(body) != std::string::npos);
}

TEST_F(HTTPResponseTest, Send404NotFound) {
    HTTPResponse response;
    const char* body = "{\"error\":\"not found\"}";

    HTTPResponse_Initiate(&response, HTTP_STATUS_404_NOT_FOUND,
                          "application/json", body, strlen(body));

    int result = HTTPResponse_Send(&response, serverSocket);
    EXPECT_EQ(result, 0);

    std::string received = ReceiveResponse();
    EXPECT_TRUE(received.find("HTTP/1.1 404 Not Found") != std::string::npos);
    EXPECT_TRUE(received.find(body) != std::string::npos);
}

TEST_F(HTTPResponseTest, SendJsonHelper) {
    const char* json = "{\"temp\":25.5,\"humidity\":60}";

    int result = HTTPResponse_SendJson(serverSocket, json);
    EXPECT_EQ(result, 0);

    std::string received = ReceiveResponse();
    EXPECT_TRUE(received.find("HTTP/1.1 200 OK") != std::string::npos);
    EXPECT_TRUE(received.find("Content-Type: application/json") != std::string::npos);
    EXPECT_TRUE(received.find(json) != std::string::npos);
}

TEST_F(HTTPResponseTest, SendErrorHelper) {
    int result = HTTPResponse_SendError(serverSocket, HTTP_STATUS_401_UNAUTHORIZED,
                                        "Invalid token");
    EXPECT_EQ(result, 0);

    std::string received = ReceiveResponse();
    EXPECT_TRUE(received.find("HTTP/1.1 401 Unauthorized") != std::string::npos);
    EXPECT_TRUE(received.find("Content-Type: application/json") != std::string::npos);
    EXPECT_TRUE(received.find("{\"error\":\"Invalid token\"}") != std::string::npos);
}

TEST_F(HTTPResponseTest, AllStatusCodes) {
    const struct {
        HTTPStatusCode code;
        const char* expected;
    } testCases[] = {
        {HTTP_STATUS_200_OK, "200 OK"},
        {HTTP_STATUS_400_BAD_REQUEST, "400 Bad Request"},
        {HTTP_STATUS_401_UNAUTHORIZED, "401 Unauthorized"},
        {HTTP_STATUS_404_NOT_FOUND, "404 Not Found"},
        {HTTP_STATUS_500_INTERNAL_SERVER_ERROR, "500 Internal Server Error"},
    };

    for (const auto& test : testCases) {
        HTTPResponse response;
        HTTPResponse_Initiate(&response, test.code, "text/plain", "test", 4);

        HTTPResponse_Send(&response, serverSocket);
        std::string received = ReceiveResponse();

        EXPECT_TRUE(received.find(test.expected) != std::string::npos)
            << "Failed for status code: " << test.code;
    }
}

TEST_F(HTTPResponseTest, SendWithInvalidFd) {
    HTTPResponse response;
    HTTPResponse_Initiate(&response, HTTP_STATUS_200_OK, "text/plain", "test", 4);

    int result = HTTPResponse_Send(&response, -1);
    EXPECT_EQ(result, -1);
}

TEST_F(HTTPResponseTest, SendNullResponse) {
    int result = HTTPResponse_Send(nullptr, serverSocket);
    EXPECT_EQ(result, -1);
}

TEST_F(HTTPResponseTest, SendJsonNullPointers) {
    int result = HTTPResponse_SendJson(-1, "{\"test\":1}");
    EXPECT_EQ(result, -1);

    result = HTTPResponse_SendJson(serverSocket, nullptr);
    EXPECT_EQ(result, -1);
}

TEST_F(HTTPResponseTest, SendErrorNullPointers) {
    int result = HTTPResponse_SendError(-1, HTTP_STATUS_500_INTERNAL_SERVER_ERROR,
                                        "error");
    EXPECT_EQ(result, -1);

    result = HTTPResponse_SendError(serverSocket, HTTP_STATUS_500_INTERNAL_SERVER_ERROR,
                                    nullptr);
    EXPECT_EQ(result, -1);
}

TEST_F(HTTPResponseTest, EmptyBody) {
    HTTPResponse response;
    HTTPResponse_Initiate(&response, HTTP_STATUS_200_OK, "text/plain", nullptr, 0);

    int result = HTTPResponse_Send(&response, serverSocket);
    EXPECT_EQ(result, 0);

    std::string received = ReceiveResponse();
    EXPECT_TRUE(received.find("Content-Length: 0") != std::string::npos);
}

TEST_F(HTTPResponseTest, LargeBody) {
    std::string largeBody(1024, 'X');
    HTTPResponse response;
    HTTPResponse_Initiate(&response, HTTP_STATUS_200_OK, "text/plain",
                          largeBody.c_str(), largeBody.size());

    int result = HTTPResponse_Send(&response, serverSocket);
    EXPECT_EQ(result, 0);

    std::string received = ReceiveResponse();
    EXPECT_TRUE(received.find("Content-Length: 1024") != std::string::npos);
    EXPECT_TRUE(received.find(largeBody) != std::string::npos);
}
