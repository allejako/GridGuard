#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Parser.h"
#include "cJSON.h"
#include "Logger.h"

int Parser_Initiate(Parser *parser)
{
    if (!parser)
        return -1;

    parser->isInitialized = true;
    LOG_INFO("Parser initialized");
    return 0;
}

int Parser_ParseWeatherData(Parser *parser, const char *jsonData, WeatherForecast *forecast)
{
    if (!parser || !parser->isInitialized || !jsonData || !forecast)
    {
        LOG_ERROR("Invalid parameters for Parser_ParseWeatherData");
        return -1;
    }

    cJSON *root = cJSON_Parse(jsonData);
    if (!root)
    {
        LOG_ERROR("Failed to parse weather JSON: %s", cJSON_GetErrorPtr());
        return -1;
    }

    // Open-Meteo format: hourly.time[], hourly.temperature_2m[], etc.
    cJSON *hourly = cJSON_GetObjectItem(root, "hourly");
    if (!cJSON_IsObject(hourly))
    {
        LOG_ERROR("Weather JSON missing 'hourly' object");
        cJSON_Delete(root);
        return -1;
    }

    cJSON *times = cJSON_GetObjectItem(hourly, "time");
    cJSON *temps = cJSON_GetObjectItem(hourly, "temperature_2m");
    cJSON *humidity = cJSON_GetObjectItem(hourly, "relative_humidity_2m");
    cJSON *clouds = cJSON_GetObjectItem(hourly, "cloud_cover");
    cJSON *winds = cJSON_GetObjectItem(hourly, "wind_speed_10m");
    cJSON *solar = cJSON_GetObjectItem(hourly, "shortwave_radiation");

    if (!cJSON_IsArray(times))
    {
        LOG_ERROR("Weather JSON missing 'hourly.time' array");
        cJSON_Delete(root);
        return -1;
    }

    int arraySize = cJSON_GetArraySize(times);
    int parsedCount = 0;

    for (int i = 0; i < arraySize && parsedCount < 96; i++)
    {
        WeatherData *data = &forecast->forecasts[parsedCount];

        // Parse timestamp (format: "YYYY-MM-DDTHH:MM")
        cJSON *timeItem = cJSON_GetArrayItem(times, i);
        if (cJSON_IsString(timeItem))
        {
            struct tm tm = {0};
            if (sscanf(timeItem->valuestring, "%d-%d-%dT%d:%d",
                      &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                      &tm.tm_hour, &tm.tm_min) == 5)
            {
                tm.tm_year -= 1900;
                tm.tm_mon -= 1;
                data->timestamp = mktime(&tm);
            }
            else
            {
                data->timestamp = time(NULL);
            }
        }

        // Parse values from parallel arrays
        cJSON *tempVal = cJSON_GetArrayItem(temps, i);
        data->temperature = cJSON_IsNumber(tempVal) ? tempVal->valuedouble : 0.0;

        cJSON *humVal = cJSON_GetArrayItem(humidity, i);
        data->humidity = cJSON_IsNumber(humVal) ? humVal->valuedouble : 0.0;

        cJSON *cloudVal = cJSON_GetArrayItem(clouds, i);
        data->cloudCover = cJSON_IsNumber(cloudVal) ? cloudVal->valuedouble : 0.0;

        cJSON *windVal = cJSON_GetArrayItem(winds, i);
        data->windSpeed = cJSON_IsNumber(windVal) ? windVal->valuedouble : 0.0;

        cJSON *solarVal = cJSON_GetArrayItem(solar, i);
        data->solarIrradiance = cJSON_IsNumber(solarVal) ? solarVal->valuedouble : 0.0;

        data->valid = true;

        // Validate data
        if (WeatherData_IsValid(data))
            parsedCount++;
        else
            LOG_WARNING("Invalid weather data at index %d, skipping", i);
    }

    forecast->count = parsedCount;
    forecast->lastUpdated = time(NULL);

    cJSON_Delete(root);
    LOG_INFO("Parsed %d weather forecasts", parsedCount);
    return 0;
}

int Parser_ParseSpotPrices(Parser *parser, const char *jsonData, SpotPriceData *spotData)
{
    if (!parser || !parser->isInitialized || !jsonData || !spotData)
    {
        LOG_ERROR("Invalid parameters for Parser_ParseSpotPrices");
        return -1;
    }

    cJSON *root = cJSON_Parse(jsonData);
    if (!root)
    {
        LOG_ERROR("Failed to parse spot price JSON: %s", cJSON_GetErrorPtr());
        return -1;
    }

    // Handle array directly (elprisetjustnu.se returns array)
    if (!cJSON_IsArray(root))
    {
        LOG_ERROR("Spot price JSON is not an array");
        cJSON_Delete(root);
        return -1;
    }

    int arraySize = cJSON_GetArraySize(root);
    int parsedCount = 0;

    for (int i = 0; i < arraySize && parsedCount < 96; i++)
    {
        cJSON *item = cJSON_GetArrayItem(root, i);
        if (!item)
            continue;

        SpotPrice *price = &spotData->prices[parsedCount];

        // Parse timestamp (ISO 8601 format or unix timestamp)
        cJSON *timeStart = cJSON_GetObjectItem(item, "time_start");
        if (cJSON_IsString(timeStart))
        {
            // Simple parsing - assumes format "YYYY-MM-DDTHH:MM:SS"
            struct tm tm = {0};
            if (sscanf(timeStart->valuestring, "%d-%d-%dT%d:%d:%d",
                      &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                      &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6)
            {
                tm.tm_year -= 1900;
                tm.tm_mon -= 1;
                price->timestamp = mktime(&tm);
            }
            else
            {
                price->timestamp = time(NULL);
            }
        }
        else
        {
            price->timestamp = time(NULL) + (i * 3600);
        }

        // Parse price in SEK/kWh
        cJSON *sekPerKwh = cJSON_GetObjectItem(item, "SEK_per_kWh");
        if (cJSON_IsNumber(sekPerKwh))
            price->priceSekPerKwh = sekPerKwh->valuedouble;
        else
            price->priceSekPerKwh = 0.0;

        // Parse price in EUR/MWh
        cJSON *eurPerMwh = cJSON_GetObjectItem(item, "EUR_per_mwh");
        if (cJSON_IsNumber(eurPerMwh))
            price->priceEurPerMwh = eurPerMwh->valuedouble;
        else
            price->priceEurPerMwh = 0.0;

        // Parse region
        cJSON *priceArea = cJSON_GetObjectItem(item, "price_area");
        if (cJSON_IsString(priceArea))
        {
            strncpy(price->region, priceArea->valuestring, sizeof(price->region) - 1);
            price->region[sizeof(price->region) - 1] = '\0';
        }
        else
        {
            strcpy(price->region, "SE3");
        }

        price->valid = true;

        // Validate data
        if (SpotPrice_IsValid(price))
        {
            parsedCount++;
        }
        else
        {
            LOG_WARNING("Invalid spot price at index %d, skipping", i);
        }
    }

    spotData->count = parsedCount;
    spotData->lastUpdated = time(NULL);

    cJSON_Delete(root);
    LOG_INFO("Parsed %d spot prices", parsedCount);
    return 0;
}

void Parser_Shutdown(Parser *parser)
{
    if (!parser || !parser->isInitialized)
        return;

    parser->isInitialized = false;
    LOG_INFO("Parser shutdown");
}
