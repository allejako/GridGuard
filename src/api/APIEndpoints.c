#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "api/APIEndpoints.h"
#include "domain/Config.h"

// Resolve current date in Europe/Stockholm timezone.
// Elprisetjustnu prices are published and indexed by Swedish calendar date,
// so using server-local time would yield wrong dates on UTC or other-TZ servers.
static struct tm get_stockholm_date(time_t t)
{
    struct tm result;
    char *old_tz = getenv("TZ");
    setenv("TZ", "Europe/Stockholm", 1);
    tzset();
    localtime_r(&t, &result);
    if (old_tz) setenv("TZ", old_tz, 1); else unsetenv("TZ");
    tzset();
    return result;
}

int BuildOpenMeteoApiUrl(char *buffer, size_t bufferSize, const char *lat, const char *lon)
{
    if (!buffer || bufferSize < 512 || !lat || !lon)
        return -1;

    // Open-Meteo forecast API with 48-hour, 15-minute granularity (192 quarters)
    // Using minutely_15 to match Elprisetjustnu's quarter-hour price data (from Oct 1, 2025)
    // CRITICAL: shortwave_radiation for solar panel calculations
    // 48h forecast = today (96 quarters) + tomorrow (96 quarters)
    int written = snprintf(buffer, bufferSize,
        "%s?latitude=%s&longitude=%s"
        "&minutely_15=temperature_2m,relative_humidity_2m,cloud_cover,"
        "wind_speed_10m,shortwave_radiation"
        "&timezone=%s&forecast_minutely_15=192",
        OPENMETEO_API_BASE_URL,
        lat,
        lon,
        WEATHER_TIMEZONE);

    if (written < 0 || (size_t)written >= bufferSize)
        return -1;

    return 0;
}

int BuildSpotPriceApiUrl(char *buffer, size_t bufferSize, const char *region, const struct tm *date)
{
    if (!buffer || bufferSize < 128 || !region)
        return -1;

    struct tm localDate;
    if (date)
    {
        localDate = *date;
    }
    else
    {
        // Always use Stockholm calendar date — elprisetjustnu URLs are indexed
        // by Swedish date, not server-local date.
        localDate = get_stockholm_date(time(NULL));
    }

    // Format: https://www.elprisetjustnu.se/api/v1/prices/2024/01-15_SE3.json
    int written = snprintf(buffer, bufferSize,
        "%s/%04d/%02d-%02d_%s.json",
        SPOTPRICE_API_BASE_URL,
        localDate.tm_year + 1900,
        localDate.tm_mon + 1,
        localDate.tm_mday,
        region);

    if (written < 0 || (size_t)written >= bufferSize)
        return -1;

    return 0;
}

int BuildSpotPriceTomorrowUrl(char *buffer, size_t bufferSize, const char *region)
{
    if (!buffer || bufferSize < 128 || !region)
        return -1;

    // Compute tomorrow's date in Stockholm timezone using mktime so that DST
    // transitions (e.g. the spring-forward night when the day is 23 h) are
    // handled correctly.  Adding 86400 seconds can land on the same calendar
    // date when the clock moves forward at 02:00 → 03:00.
    struct tm tomorrowDate = get_stockholm_date(time(NULL));
    tomorrowDate.tm_mday += 1;
    tomorrowDate.tm_isdst = -1;  // let mktime determine DST for the new date
    {
        char *old_tz = getenv("TZ");
        setenv("TZ", "Europe/Stockholm", 1);
        tzset();
        mktime(&tomorrowDate);  // normalise day/month/year overflow
        if (old_tz) setenv("TZ", old_tz, 1); else unsetenv("TZ");
        tzset();
    }

    return BuildSpotPriceApiUrl(buffer, bufferSize, region, &tomorrowDate);
}
