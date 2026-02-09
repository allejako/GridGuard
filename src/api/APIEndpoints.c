#include <stdio.h>
#include <string.h>
#include <time.h>
#include "APIEndpoints.h"
#include "Config.h"

int BuildWeatherApiUrl(char *buffer, size_t bufferSize, const char *lat, const char *lon)
{
    if (!buffer || bufferSize < 256 || !lat || !lon)
        return -1;

    // Open-Meteo forecast API
    int written = snprintf(buffer, bufferSize,
        "%s?latitude=%s&longitude=%s"
        "&hourly=temperature_2m,relative_humidity_2m,cloud_cover,wind_speed_10m,shortwave_radiation"
        "&timezone=%s&forecast_days=1",
        WEATHER_API_BASE_URL,
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
