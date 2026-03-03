#pragma once

// ── C ↔ C++ integration ──────────────────────────────────────────────────────
//
// UserConfigWrapper provides a C++ class interface around the plain-C
// UserConfig struct defined in src/application/models/config/UserConfig.h.
//
// The free functions at the bottom (declared extern "C") expose selected
// functionality back to C code that may need to call into this module.
//
// Course relevance (Kurs 3 — Systemprogrammering och introduktion till C++):
//   • Week 6  — Fungerande integration mellan C och C++-kod (extern "C")
//   • Week 8  — RAII (constructor validates + owns the struct)
//   • Week 9  — std::string, std::map via getAsMap()

#include "../application/models/config/UserConfig.h"  // plain-C struct

#include <string>
#include <map>
#include <stdexcept>

namespace gridguard {

// ── C++ wrapper ──────────────────────────────────────────────────────────────

class UserConfigWrapper {
public:
    // Construct from an existing C struct (copies all fields).
    // Throws std::invalid_argument if the struct contains invalid data.
    explicit UserConfigWrapper(const UserConfig& cfg);

    // Construct with explicit values (validates immediately).
    UserConfigWrapper(const std::string& userId,
                      double lat, double lon,
                      const std::string& region,
                      double solarAreaM2,
                      double solarEfficiency,
                      double consumptionKwh);

    // Return a read-only reference to the underlying C struct.
    const UserConfig& get() const noexcept { return cfg_; }

    // Convenience getters returning std::string instead of char[].
    std::string userId()   const { return cfg_.userId; }
    std::string location() const { return cfg_.location; }
    std::string region()   const { return cfg_.region; }

    // Return all config fields as a std::map (useful for serialisation / display).
    std::map<std::string, std::string> getAsMap() const;

    // Validate the struct; throws std::invalid_argument on failure.
    static void validate(const UserConfig& cfg);

private:
    UserConfig cfg_{};

    static void copyStr(char* dst, size_t dstSize, const std::string& src);
};

} // namespace gridguard

// ── extern "C" bridge ────────────────────────────────────────────────────────
// These functions have C linkage so that plain C translation units can call them.

#ifdef __cplusplus
extern "C" {
#endif

// Validates a UserConfig struct.  Returns 1 if valid, 0 if not.
int userconfig_validate(const UserConfig* cfg);

// Fills dst with a human-readable summary of cfg (null-terminated).
// Returns the number of characters written (excluding null terminator).
int userconfig_summary(const UserConfig* cfg, char* dst, int dstSize);

#ifdef __cplusplus
}
#endif
