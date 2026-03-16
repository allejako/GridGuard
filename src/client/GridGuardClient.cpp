#include "GridGuardClient.hpp"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <stdexcept>

namespace gridguard {

// ── Constructor ─────────────────────────────────────────────────────────────

GridGuardClient::GridGuardClient(const std::string& host, int port, std::string token)
    : http_(std::make_unique<HttpClient>(host, port))
    , token_(std::move(token))
{}

// ── JSON helpers ────────────────────────────────────────────────────────────

std::string GridGuardClient::extractStr(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return {};
    pos += search.size();
    auto end = json.find('"', pos);
    if (end == std::string::npos) return {};
    return json.substr(pos, end - pos);
}

double GridGuardClient::extractDouble(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return 0.0;
    pos += search.size();
    try { return std::stod(json.substr(pos)); }
    catch (...) { return 0.0; }
}

int GridGuardClient::extractInt(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return 0;
    pos += search.size();
    try { return std::stoi(json.substr(pos)); }
    catch (...) { return 0; }
}

// Splits a JSON array identified by 'key' into individual object strings.
// Example: {"forecast":[{...},{...}]} → ["{...}", "{...}"]
std::vector<std::string> GridGuardClient::splitJsonArray(const std::string& json,
                                                          const std::string& key) {
    std::vector<std::string> result;

    std::string search = "\"" + key + "\":[";
    auto pos = json.find(search);
    if (pos == std::string::npos) return result;
    pos += search.size();

    while (pos < json.size()) {
        // Skip separators
        while (pos < json.size() && (json[pos] == ',' || json[pos] == ' ' ||
               json[pos] == '\n' || json[pos] == '\r'))
            ++pos;

        if (pos >= json.size() || json[pos] == ']') break;

        if (json[pos] == '{') {
            int depth     = 1;
            size_t objStart = pos++;
            while (pos < json.size() && depth > 0) {
                if      (json[pos] == '{') ++depth;
                else if (json[pos] == '}') --depth;
                ++pos;
            }
            result.push_back(json.substr(objStart, pos - objStart));
        } else {
            ++pos;
        }
    }
    return result;
}

std::vector<ForecastEntry> GridGuardClient::parseForecastArray(const std::string& json) {
    std::vector<ForecastEntry> entries;
    // Server returns: {"days":[{"date":"...","hours":[{entry},{entry},...]},...]
    for (const auto& day : splitJsonArray(json, "days")) {
        for (const auto& obj : splitJsonArray(day, "hours")) {
            ForecastEntry e;
            e.time            = extractStr   (obj, "time");
            e.signal          = extractStr   (obj, "signal");
            e.priceSekKwh     = extractDouble(obj, "price_sek_kwh");
            e.totalCostSekKwh = extractDouble(obj, "total_cost_sek_kwh");
            e.savingsVsMedian = extractDouble(obj, "price_vs_avg_pct");
            e.solarKwh        = extractDouble(obj, "solar_kwh");
            e.consumptionKwh  = extractDouble(obj, "consumption_kwh");
            entries.push_back(std::move(e));
        }
    }
    return entries;
}

std::vector<ScheduleEntry> GridGuardClient::parseScheduleArray(const std::string& json) {
    std::vector<ScheduleEntry> entries;
    for (const auto& obj : splitJsonArray(json, "schedules")) {
        ScheduleEntry e;
        e.scheduleId      = extractStr   (obj, "schedule_id");
        e.loadId          = extractStr   (obj, "load_id");
        e.scheduledStart  = extractStr   (obj, "scheduled_start");
        e.durationMinutes = extractInt   (obj, "duration_minutes");
        e.powerKw         = extractDouble(obj, "power_kw");
        e.estimatedCostSek= extractDouble(obj, "estimated_cost_sek");
        e.savingsSek      = extractDouble(obj, "savings_sek");
        e.status          = extractStr   (obj, "status");
        entries.push_back(std::move(e));
    }
    return entries;
}

// ── API methods ─────────────────────────────────────────────────────────────

bool GridGuardClient::checkHealth() {
    auto resp = http_->get("/health");
    return resp.ok();
}

std::vector<ForecastEntry> GridGuardClient::getForecast(ForecastSummary& summary) {
    auto resp = http_->get("/forecast", token_);
    if (!resp.ok()) return {};

    summary.userId       = extractStr   (resp.body, "user_id");
    summary.location     = extractStr   (resp.body, "location");
    summary.region       = extractStr   (resp.body, "region");
    summary.entries      = extractInt   (resp.body, "forecast_hours");
    summary.totalCostSek = extractDouble(resp.body, "total_cost_sek");
    summary.gridImportKwh= extractDouble(resp.body, "grid_import_kwh");
    summary.gridExportKwh= extractDouble(resp.body, "grid_export_kwh");

    return parseForecastArray(resp.body);
}

std::string GridGuardClient::getUserConfig() {
    auto resp = http_->get("/user/config", token_);
    if (!resp.ok()) return {};
    return resp.body;
}

bool GridGuardClient::setUserConfig(double lat, double lon,
                                     const std::string& region,
                                     const std::string& location,
                                     double solarAreaM2,
                                     double solarEfficiency,
                                     double consumptionKwh) {
    std::ostringstream body;
    body << "{\"latitude\":"       << lat
         << ",\"longitude\":"      << lon
         << ",\"region\":\""       << region         << "\""
         << ",\"location\":\""     << location       << "\""
         << ",\"solar_area_m2\":"  << solarAreaM2
         << ",\"solar_efficiency\":"<< solarEfficiency
         << ",\"consumption_kwh\":"  << consumptionKwh
         << "}";

    auto resp = http_->put("/user/config", body.str(), token_);
    return resp.ok();
}

std::vector<ScheduleEntry> GridGuardClient::getSchedules() {
    auto resp = http_->get("/schedule", token_);
    if (!resp.ok()) return {};
    return parseScheduleArray(resp.body);
}

bool GridGuardClient::createSchedule(const std::string& loadId,
                                      int durationMinutes,
                                      double powerKw,
                                      long deadlineUnix) {
    std::ostringstream body;
    body << "{\"load_id\":\""        << loadId          << "\""
         << ",\"duration_minutes\":" << durationMinutes
         << ",\"power_kw\":"         << powerKw;
    if (deadlineUnix > 0)
        body << ",\"deadline\":" << deadlineUnix;
    body << "}";

    auto resp = http_->post("/schedule", body.str(), token_);
    if (resp.ok()) {
        std::cout << resp.body << "\n";
    } else {
        std::cerr << "Error: " << resp.body << "\n";
    }
    return resp.ok();
}

bool GridGuardClient::deleteSchedule(const std::string& scheduleId) {
    auto resp = http_->del("/schedule/" + scheduleId, token_);
    return resp.ok();
}

} // namespace gridguard
