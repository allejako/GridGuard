#ifndef _API_ENDPOINTS_H_
#define _API_ENDPOINTS_H_

#include <time.h>

// ── Weather API URL builders ──────────────────────────────────────────────────
// One function per weather source. FetchWorker calls these to build the URL
// before fetching. The response is parsed in Parser.c and mapped to WeatherData
// in ParseWorker via a WeatherFrom<Source>() function.
//
// To add a new weather source:
//   1. Add Build<Source>ApiUrl() here and implement it in APIEndpoints.c
//   2. Add <Source>Response.h to models/apis/ and Parser_Parse<Source>() to Parser.h/c
//   3. Add WeatherFrom<Source>() in ParseWorker.c
// ─────────────────────────────────────────────────────────────────────────────

// Open-Meteo: temperature, humidity, cloud cover, wind speed, solar irradiance
// Requires: lat/lon as decimal strings, buffer >= 512 bytes
// Returns: 0 on success, -1 on error
int BuildOpenMeteoApiUrl(char *buffer, size_t bufferSize, const char *lat, const char *lon);

// ── Spot price API URL builders ───────────────────────────────────────────────
// One function per spot price source. FetchWorker calls these to build the URL.
// The response is parsed in Parser.c and mapped to SpotPrice in ParseWorker
// via ConvertToSpotPrice().
//
// To add a new spot price source:
//   1. Add Build<Source>ApiUrl() here and implement it in APIEndpoints.c
//   2. Add <Source>Response.h to models/apis/ and Parser_Parse<Source>() to Parser.h/c
//   3. Add a ConvertFrom<Source>() in ParseWorker.c or extend ConvertToSpotPrice()
// ─────────────────────────────────────────────────────────────────────────────

// Elpriset.se: Swedish spot prices per region (SE1–SE4), today or given date
// Requires: region string (e.g. "SE3"), buffer >= 128 bytes
// date == NULL uses today's date
// Returns: 0 on success, -1 on error
int BuildSpotPriceApiUrl(char *buffer, size_t bufferSize, const char *region, const struct tm *date);

// Convenience wrapper for tomorrow's prices (published ~13:00 each day)
int BuildSpotPriceTomorrowUrl(char *buffer, size_t bufferSize, const char *region);

#endif
