#pragma once

#include "HttpClient.hpp"

#include <string>
#include <vector>
#include <memory>

namespace gridguard {

// ── Data models ────────────────────────────────────────────────────────────

struct ForecastEntry {
    std::string time;
    std::string signal;        // "BUY" | "SELL" | "IDLE"
    double      priceSekKwh{0.0};
    double      totalCostSekKwh{0.0};
    double      savingsVsMedian{0.0};
    double      solarKwh{0.0};
    double      consumptionKwh{0.0};
};

struct ForecastSummary {
    std::string userId;
    std::string location;
    std::string region;
    int         entries{0};         // forecast_hours (24h)
    int         quarters{0};        // forecast_quarters (96× 15min)
    double      totalCostSek{0.0};
    double      gridImportKwh{0.0};
    double      gridExportKwh{0.0};
};

struct ScheduleEntry {
    std::string scheduleId;
    std::string loadId;
    std::string scheduledStart;
    int         durationMinutes{0};
    double      powerKw{0.0};
    double      estimatedCostSek{0.0};
    double      savingsSek{0.0};
    std::string status;
};

// ── Client ─────────────────────────────────────────────────────────────────

class GridGuardClient {
public:
    GridGuardClient(const std::string& host, int port, std::string token);

    // Server status
    bool checkHealth();

    // Forecast — returns parsed entries + fills summary out-param
    std::vector<ForecastEntry> getForecast(ForecastSummary& summary);

    // User configuration
    std::string getUserConfig();
    bool setUserConfig(double lat, double lon,
                       const std::string& region,
                       const std::string& location,
                       double solarAreaM2,
                       double solarEfficiency,
                       double consumptionKwh);

    // Schedule management
    std::vector<ScheduleEntry> getSchedules();
    bool createSchedule(const std::string& loadId,
                        int durationMinutes,
                        double powerKw,
                        long deadlineUnix = 0);
    bool deleteSchedule(const std::string& scheduleId);

private:
    std::unique_ptr<HttpClient> http_;
    std::string                 token_;

    // ── JSON helpers ────────────────────────────────────────────────────────

    // Extract a quoted string value: "key":"value"
    static std::string extractStr(const std::string& json, const std::string& key);

    // Extract a numeric value: "key":3.14
    static double extractDouble(const std::string& json, const std::string& key);

    // Extract an integer value: "key":42
    static int extractInt(const std::string& json, const std::string& key);

    // Split a JSON array into individual object strings
    static std::vector<std::string> splitJsonArray(const std::string& json,
                                                   const std::string& key);

    static std::vector<ForecastEntry> parseForecastArray(const std::string& json);
    static std::vector<ScheduleEntry> parseScheduleArray(const std::string& json);
};

} // namespace gridguard
