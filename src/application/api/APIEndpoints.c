#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "APIEndpoints.h"
#include "Config.h"

int BuildSMHIApiUrl(char *buffer, size_t bufferSize, const char *lat, const char *lon)
{
    if (!buffer || bufferSize < 512 || !lat || !lon)
        return -1;

    // SMHI Point Forecast API
    // Endpoint: /api/category/pmp3g/version/2/geotype/point/lon/{lon}/lat/{lat}/data.json
    // IMPORTANT: SMHI uses lon/lat order (opposite of most APIs)
    int written = snprintf(buffer, bufferSize,
        "%s/geotype/point/lon/%s/lat/%s/data.json",
        SMHI_API_BASE_URL,
        lon,  // Note: lon before lat!
        lat);

    if (written < 0 || (size_t)written >= bufferSize)
        return -1;

    return 0;
}

int BuildOpenMeteoApiUrl(char *buffer, size_t bufferSize, const char *lat, const char *lon)
{
    if (!buffer || bufferSize < 512 || !lat || !lon)
        return -1;

    // Open-Meteo forecast API
    // Focus on parameters needed for GridGuard energy forecasting
    // CRITICAL: shortwave_radiation for solar panel calculations
    int written = snprintf(buffer, bufferSize,
        "%s?latitude=%s&longitude=%s"
        "&hourly=temperature_2m,relative_humidity_2m,cloud_cover,"
        "wind_speed_10m,shortwave_radiation,direct_radiation,diffuse_radiation"
        "&timezone=%s&forecast_days=4",
        OPENMETEO_API_BASE_URL,
        lat,
        lon,
        WEATHER_TIMEZONE);

    if (written < 0 || (size_t)written >= bufferSize)
        return -1;

    return 0;
}

// DEPRECATED: Kept for backward compatibility
int BuildWeatherApiUrl(char *buffer, size_t bufferSize, const char *lat, const char *lon)
{
    return BuildOpenMeteoApiUrl(buffer, bufferSize, lat, lon);
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
        // Använd dagens datum
        time_t now = time(NULL);
        localtime_r(&now, &localDate);
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

    // Morgondagens datum
    time_t tomorrow = time(NULL) + (24 * 3600);
    struct tm tomorrowDate;
    localtime_r(&tomorrow, &tomorrowDate);

    return BuildSpotPriceApiUrl(buffer, bufferSize, region, &tomorrowDate);
}
