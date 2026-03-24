#include "UserConfigWrapper.hpp"

#include <cstring>
#include <cstdio>
#include <sstream>
#include <iomanip>

namespace gridguard {

// ── helpers ──────────────────────────────────────────────────────────────────

void UserConfigWrapper::copyStr(char* dst, size_t dstSize, const std::string& src) {
    std::strncpy(dst, src.c_str(), dstSize - 1);
    dst[dstSize - 1] = '\0';
}

// ── validate ─────────────────────────────────────────────────────────────────

void UserConfigWrapper::validate(const UserConfig& cfg) {
    if (cfg.latitude  < -90.0  || cfg.latitude  > 90.0)
        throw std::invalid_argument("latitude out of range [-90, 90]");
    if (cfg.longitude < -180.0 || cfg.longitude > 180.0)
        throw std::invalid_argument("longitude out of range [-180, 180]");
    if (cfg.solarEfficiency < 0.0 || cfg.solarEfficiency > 1.0)
        throw std::invalid_argument("solarEfficiency must be in [0, 1]");
    if (cfg.consumptionKwh < 0.0)
        throw std::invalid_argument("consumptionKwh must be >= 0");
    if (cfg.solarAreaM2 < 0.0)
        throw std::invalid_argument("solarAreaM2 must be >= 0");
    if (cfg.panelTiltDeg < 0.0 || cfg.panelTiltDeg > 90.0)
        throw std::invalid_argument("panelTiltDeg must be in [0, 90]");
    if (cfg.panelAzimuthDeg < 0.0 || cfg.panelAzimuthDeg >= 360.0)
        throw std::invalid_argument("panelAzimuthDeg must be in [0, 360)");
}

// ── constructors ─────────────────────────────────────────────────────────────

UserConfigWrapper::UserConfigWrapper(const UserConfig& cfg) : cfg_(cfg) {
    validate(cfg_);
}

UserConfigWrapper::UserConfigWrapper(const std::string& userId,
                                     double lat, double lon,
                                     const std::string& region,
                                     double solarAreaM2,
                                     double solarEfficiency,
                                     double consumptionKwh) {
    copyStr(cfg_.userId,   sizeof(cfg_.userId),   userId);
    copyStr(cfg_.region,   sizeof(cfg_.region),   region);
    cfg_.latitude        = lat;
    cfg_.longitude       = lon;
    cfg_.solarAreaM2     = solarAreaM2;
    cfg_.solarEfficiency = solarEfficiency;
    cfg_.consumptionKwh  = consumptionKwh;
    validate(cfg_);
}

// ── getAsMap ─────────────────────────────────────────────────────────────────

std::map<std::string, std::string> UserConfigWrapper::getAsMap() const {
    auto d = [](double v, int prec = 4) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(prec) << v;
        return ss.str();
    };

    return {
        { "user_id",          cfg_.userId               },
        { "location",         cfg_.location             },
        { "region",           cfg_.region               },
        { "latitude",         d(cfg_.latitude)          },
        { "longitude",        d(cfg_.longitude)         },
        { "solar_area_m2",    d(cfg_.solarAreaM2,  2)   },
        { "solar_efficiency", d(cfg_.solarEfficiency, 3)},
        { "consumption_kwh",  d(cfg_.consumptionKwh, 3) },
        { "grid_fee_low",     d(cfg_.gridFeeLow,   4)  },
        { "grid_fee_normal",  d(cfg_.gridFeeNormal, 4) },
        { "grid_fee_high",    d(cfg_.gridFeeHigh,   4) },
        { "panel_tilt_deg",   d(cfg_.panelTiltDeg,  1) },
        { "panel_azimuth_deg",d(cfg_.panelAzimuthDeg,1)},
    };
}

} // namespace gridguard

// ── extern "C" implementations ───────────────────────────────────────────────

extern "C" {

int userconfig_validate(const UserConfig* cfg) {
    if (!cfg) return 0;
    try {
        gridguard::UserConfigWrapper::validate(*cfg);
        return 1;
    } catch (...) {
        return 0;
    }
}

int userconfig_summary(const UserConfig* cfg, char* dst, int dstSize) {
    if (!cfg || !dst || dstSize <= 0) return 0;
    try {
        gridguard::UserConfigWrapper w(*cfg);
        auto m = w.getAsMap();
        std::ostringstream ss;
        for (const auto& kv : m)
            ss << kv.first << "=" << kv.second << "\n";
        std::string s = ss.str();
        int len = static_cast<int>(s.size());
        std::strncpy(dst, s.c_str(), static_cast<size_t>(dstSize) - 1);
        dst[dstSize - 1] = '\0';
        return (len < dstSize) ? len : dstSize - 1;
    } catch (...) {
        return 0;
    }
}

} // extern "C"
