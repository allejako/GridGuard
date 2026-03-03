#include "HttpClient.hpp"
#include "SocketGuard.hpp"

#include <sys/socket.h>
#include <netdb.h>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace gridguard {

// ── Custom exceptions ────────────────────────────────────────────────────────

struct ConnectionError : std::runtime_error {
    explicit ConnectionError(const std::string& msg) : std::runtime_error(msg) {}
};

struct NetworkError : std::runtime_error {
    explicit NetworkError(const std::string& msg) : std::runtime_error(msg) {}
};

// ── Constructor ──────────────────────────────────────────────────────────────

HttpClient::HttpClient(std::string host, int port)
    : host_(std::move(host)), port_(port) {
    if (host_.empty())
        throw std::invalid_argument("HttpClient: host must not be empty");
    if (port_ <= 0 || port_ > 65535)
        throw std::invalid_argument("HttpClient: port out of range (1-65535)");
}

// ── connectToServer ──────────────────────────────────────────────────────────

int HttpClient::connectToServer() const {
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string portStr = std::to_string(port_);
    if (getaddrinfo(host_.c_str(), portStr.c_str(), &hints, &res) != 0)
        throw ConnectionError("DNS lookup failed for host: " + host_);

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        throw ConnectionError("socket() failed");
    }

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        close(fd);
        freeaddrinfo(res);
        throw ConnectionError("connect() failed to " + host_ + ":" + std::to_string(port_));
    }

    freeaddrinfo(res);
    return fd;
}

// ── send ─────────────────────────────────────────────────────────────────────

HttpResponse HttpClient::send(const std::string& method, const std::string& path,
                               const std::string& body,   const std::string& token) {
    try {
        // SocketGuard ensures the fd is closed when send() returns
        SocketGuard sock(connectToServer());

        std::ostringstream req;
        req << method << " " << path << " HTTP/1.1\r\n";
        req << "Host: " << host_ << "\r\n";
        req << "Connection: close\r\n";
        if (!token.empty())
            req << "Authorization: Bearer " << token << "\r\n";
        if (!body.empty()) {
            req << "Content-Type: application/json\r\n";
            req << "Content-Length: " << body.size() << "\r\n";
        }
        req << "\r\n";
        if (!body.empty())
            req << body;

        std::string reqStr = req.str();
        if (write(sock.get(), reqStr.c_str(), reqStr.size()) < 0)
            throw NetworkError("write() to socket failed");

        // Read full response into a string
        std::string raw;
        char buf[4096];
        ssize_t n;
        while ((n = read(sock.get(), buf, sizeof(buf))) > 0)
            raw.append(buf, static_cast<size_t>(n));

        return parseResponse(raw);

    } catch (const ConnectionError& e) {
        return {0, std::string("Connection error: ") + e.what()};
    } catch (const NetworkError& e) {
        return {0, std::string("Network error: ") + e.what()};
    } catch (const std::exception& e) {
        return {0, std::string("Unexpected error: ") + e.what()};
    }
}

HttpResponse HttpClient::parseResponse(const std::string& raw) {
    HttpResponse resp;
    if (raw.empty()) return resp;

    // Status line: "HTTP/1.1 200 OK\r\n"
    auto lineEnd = raw.find("\r\n");
    if (lineEnd == std::string::npos) return resp;

    auto spacePos = raw.find(' ');
    if (spacePos != std::string::npos && spacePos < lineEnd) {
        try {
            resp.statusCode = std::stoi(raw.substr(spacePos + 1, 3));
        } catch (...) {
            resp.statusCode = 0;
        }
    }

    // Body follows the blank line "\r\n\r\n"
    auto bodyStart = raw.find("\r\n\r\n");
    if (bodyStart != std::string::npos)
        resp.body = raw.substr(bodyStart + 4);

    return resp;
}

HttpResponse HttpClient::get(const std::string& path, const std::string& token) {
    return send("GET", path, "", token);
}

HttpResponse HttpClient::put(const std::string& path, const std::string& body,
                              const std::string& token) {
    return send("PUT", path, body, token);
}

HttpResponse HttpClient::post(const std::string& path, const std::string& body,
                               const std::string& token) {
    return send("POST", path, body, token);
}

HttpResponse HttpClient::del(const std::string& path, const std::string& token) {
    return send("DELETE", path, "", token);
}

} // namespace gridguard
