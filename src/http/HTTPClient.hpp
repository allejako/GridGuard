#ifndef __HTTPClient_hpp_
#define __HTTPClient_hpp_

#include <string>
#include <cstddef>

class HTTPClient
{
public:
    HTTPClient();
    ~HTTPClient();

    int get(const std::string &host, const std::string &port, const std::string &path);

    int getStatusCode() const { return statusCode; }
    const std::string& getBody() const { return body; }
    size_t getBodyLength() const { return body.length(); }

private:
    int statusCode;
    std::string body;
};

#endif // __HTTPClient_hpp_
