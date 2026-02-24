#include "GridGuardClient.hpp"
#include "cJSON.h"

#include <stdexcept>
#include <cstdio>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

GridGuardClient::GridGuardClient(std::string host, std::string port)
    : host_(std::move(host)), port_(std::move(port)) {}

std::string GridGuardClient::get(const std::string& path, const std::string& token)
{
    struct addrinfo hints = {}, *res;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host_.c_str(), port_.c_str(), &hints, &res) != 0)
        throw std::runtime_error("Kan inte slå upp " + host_ + ":" + port_);

    int fd = -1;
    for (auto* ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0)
        throw std::runtime_error("Ingen server körs på " + host_ + ":" + port_);

    struct timeval tv = { 60, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::string req = "GET " + path + " HTTP/1.1\r\n"
                    + "Host: " + host_ + "\r\n"
                    + "Authorization: Bearer " + token + "\r\n"
                    + "Connection: close\r\n\r\n";

    send(fd, req.c_str(), req.size(), 0);

    std::string raw;
    char buf[4096];
    ssize_t n;
    while ((n = recv(fd, buf, sizeof(buf), 0)) > 0)
        raw.append(buf, static_cast<size_t>(n));
    close(fd);

    int status = 0;
    sscanf(raw.c_str(), "HTTP/%*d.%*d %d", &status);
    if (status == 401)
        throw std::runtime_error("Ogiltig token. Kör: gridguard login <token>");
    if (status == 400)
        throw std::runtime_error("Ingen konfiguration sparad. Kör: gridguard config");
    if (status != 200)
        throw std::runtime_error("Serverfel (HTTP " + std::to_string(status) + ")");

    auto pos = raw.find("\r\n\r\n");
    if (pos == std::string::npos)
        throw std::runtime_error("Ogiltigt HTTP-svar från servern");

    return raw.substr(pos + 4);
}

ForecastResponse GridGuardClient::forecast(const std::string& token)
{
    std::string body = get("/forecast", token);

    cJSON* root = cJSON_Parse(body.c_str());
    if (!root)
        throw std::runtime_error("Kunde inte tolka svaret från servern");

    ForecastResponse resp;

    auto str = [&](cJSON* obj, const char* key) -> std::string {
        cJSON* item = cJSON_GetObjectItem(obj, key);
        return (item && item->valuestring) ? item->valuestring : "";
    };
    auto num = [&](cJSON* obj, const char* key) -> double {
        cJSON* item = obj ? cJSON_GetObjectItem(obj, key) : nullptr;
        return (item && cJSON_IsNumber(item)) ? item->valuedouble : 0.0;
    };

    resp.location     = str(root, "location");
    resp.region       = str(root, "region");

    cJSON* summary       = cJSON_GetObjectItem(root, "summary");
    resp.totalCostSek    = num(summary, "total_cost_sek");
    resp.gridImportKwh   = num(summary, "grid_import_kwh");
    resp.gridExportKwh   = num(summary, "grid_export_kwh");

    if (cJSON* arr = cJSON_GetObjectItem(root, "forecast")) {
        int count = cJSON_GetArraySize(arr);
        resp.entries.reserve(count);
        for (int i = 0; i < count; i++) {
            cJSON* e = cJSON_GetArrayItem(arr, i);
            ForecastEntry fe;
            fe.time          = str(e, "time");
            fe.signal        = str(e, "signal");
            fe.priceSek      = num(e, "price_sek_kwh");
            fe.solarKwh      = num(e, "solar_kwh");
            fe.consumptionKwh = num(e, "consumption_kwh");
            resp.entries.push_back(std::move(fe));
        }
    }

    cJSON_Delete(root);
    return resp;
}
