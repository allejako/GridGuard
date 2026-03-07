#pragma once

#include <string>

namespace gridguard {

struct HttpResponse {
    int         statusCode{0};
    std::string body;

    bool ok()  const noexcept { return statusCode >= 200 && statusCode < 300; }
    bool not_found() const noexcept { return statusCode == 404; }
    bool unauthorized() const noexcept { return statusCode == 401; }
};

// Thin HTTP/1.1 client over plain TCP.
// Each method opens a new connection, sends the request, reads the full
// response, then closes the socket via SocketGuard (RAII).
class HttpClient {
public:
    HttpClient(std::string host, int port);

    HttpResponse get (const std::string& path, const std::string& token = "");
    HttpResponse put (const std::string& path, const std::string& body,
                      const std::string& token = "");
    HttpResponse post(const std::string& path, const std::string& body,
                      const std::string& token = "");
    HttpResponse del (const std::string& path, const std::string& token = "");

private:
    std::string host_;
    int         port_;

    int          connectToServer() const;
    HttpResponse send(const std::string& method, const std::string& path,
                      const std::string& body,   const std::string& token);

    static HttpResponse parseResponse(const std::string& raw);
};

} // namespace gridguard
