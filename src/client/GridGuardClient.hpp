#pragma once

#include <string>
#include <vector>

struct ForecastEntry {
    std::string time;
    std::string signal;
    double      priceSek      = 0;
    double      solarKwh      = 0;
    double      consumptionKwh = 0;
};

struct ForecastResponse {
    std::string location;
    std::string region;
    double      totalCostSek  = 0;
    double      gridImportKwh = 0;
    double      gridExportKwh = 0;
    std::vector<ForecastEntry> entries;
};

class GridGuardClient {
public:
    GridGuardClient(std::string host, std::string port);

    ForecastResponse forecast(const std::string& token);

private:
    std::string host_;
    std::string port_;

    std::string get(const std::string& path, const std::string& token);
};
