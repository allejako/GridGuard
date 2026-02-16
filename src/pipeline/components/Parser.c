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

int Parser_ParseOpenMeteo(Parser *parser, const char *jsonData, OpenMeteoResponse *forecast)
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
        OpenMeteoEntry *data = &forecast->entries[parsedCount];

        // Parse time string (format: "YYYY-MM-DDTHH:MM")
        cJSON *timeItem = cJSON_GetArrayItem(times, i);
        if (cJSON_IsString(timeItem))
        {
            strncpy(data->time, timeItem->valuestring, sizeof(data->time) - 1);
            data->time[sizeof(data->time) - 1] = '\0';
        }
        else
        {
            data->time[0] = '\0';
        }

        // Parse values from parallel arrays
        cJSON *tempVal = cJSON_GetArrayItem(temps, i);
        data->temperature_2m = cJSON_IsNumber(tempVal) ? tempVal->valuedouble : 0.0;

        cJSON *humVal = cJSON_GetArrayItem(humidity, i);
        data->humidity_2m = cJSON_IsNumber(humVal) ? humVal->valuedouble : 0.0;

        cJSON *cloudVal = cJSON_GetArrayItem(clouds, i);
        data->cloud_cover = cJSON_IsNumber(cloudVal) ? cloudVal->valuedouble : 0.0;

        cJSON *windVal = cJSON_GetArrayItem(winds, i);
        data->wind_speed_10m = cJSON_IsNumber(windVal) ? windVal->valuedouble : 0.0;

        cJSON *solarVal = cJSON_GetArrayItem(solar, i);
        data->shortwave_radiation = cJSON_IsNumber(solarVal) ? solarVal->valuedouble : 0.0;

        parsedCount++;
    }

    forecast->count = parsedCount;

    cJSON_Delete(root);
    LOG_INFO("Parsed %d weather forecasts", parsedCount);
    return 0;
}

int Parser_ParseElpriset(Parser *parser, const char *jsonData, ElprisetResponse *spotData)
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

        ElprisetEntry *price = &spotData->entries[parsedCount];

        // Parse time_start string
        cJSON *timeStart = cJSON_GetObjectItem(item, "time_start");
        if (cJSON_IsString(timeStart))
        {
            strncpy(price->time_start, timeStart->valuestring, sizeof(price->time_start) - 1);
            price->time_start[sizeof(price->time_start) - 1] = '\0';
        }
        else
        {
            price->time_start[0] = '\0';
        }

        // Parse time_end string
        cJSON *timeEnd = cJSON_GetObjectItem(item, "time_end");
        if (cJSON_IsString(timeEnd))
        {
            strncpy(price->time_end, timeEnd->valuestring, sizeof(price->time_end) - 1);
            price->time_end[sizeof(price->time_end) - 1] = '\0';
        }
        else
        {
            price->time_end[0] = '\0';
        }

        // Parse price in SEK/kWh
        cJSON *sekPerKwh = cJSON_GetObjectItem(item, "SEK_per_kWh");
        price->SEK_per_kWh = cJSON_IsNumber(sekPerKwh) ? sekPerKwh->valuedouble : 0.0;

        // Parse price in EUR/kWh
        cJSON *eurPerKwh = cJSON_GetObjectItem(item, "EUR_per_kWh");
        price->EUR_per_kWh = cJSON_IsNumber(eurPerKwh) ? eurPerKwh->valuedouble : 0.0;

        // Parse exchange rate
        cJSON *exr = cJSON_GetObjectItem(item, "EXR");
        price->EXR = cJSON_IsNumber(exr) ? exr->valuedouble : 0.0;

        parsedCount++;
    }

    spotData->count = parsedCount;

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
