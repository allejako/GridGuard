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

int Parser_ParseOpenMeteo(Parser *parser, const char *jsonData, OpenMeteoResponse *response)
{
    if (!parser || !parser->isInitialized || !jsonData || !response)
    {
        LOG_ERROR("Invalid parameters for Parser_ParseOpenMeteo");
        return -1;
    }

    cJSON *root = cJSON_Parse(jsonData);
    if (!root)
    {
        LOG_ERROR("Failed to parse Open-Meteo JSON: %s", cJSON_GetErrorPtr());
        return -1;
    }

    cJSON *hourly = cJSON_GetObjectItem(root, "hourly");
    if (!cJSON_IsObject(hourly))
    {
        LOG_ERROR("Open-Meteo JSON missing 'hourly' object");
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
        LOG_ERROR("Open-Meteo JSON missing 'hourly.time' array");
        cJSON_Delete(root);
        return -1;
    }

    int arraySize = cJSON_GetArraySize(times);
    int parsedCount = 0;

    for (int i = 0; i < arraySize && parsedCount < 96; i++)
    {
        OpenMeteoEntry *entry = &response->entries[parsedCount];

        cJSON *timeItem = cJSON_GetArrayItem(times, i);
        if (cJSON_IsString(timeItem))
        {
            strncpy(entry->time, timeItem->valuestring, sizeof(entry->time) - 1);
            entry->time[sizeof(entry->time) - 1] = '\0';
        }
        else
        {
            entry->time[0] = '\0';
            continue;
        }

        cJSON *tempVal = cJSON_GetArrayItem(temps, i);
        entry->temperature_2m = cJSON_IsNumber(tempVal) ? tempVal->valuedouble : 0.0;

        cJSON *humVal = cJSON_GetArrayItem(humidity, i);
        entry->humidity_2m = cJSON_IsNumber(humVal) ? humVal->valuedouble : 0.0;

        cJSON *cloudVal = cJSON_GetArrayItem(clouds, i);
        entry->cloud_cover = cJSON_IsNumber(cloudVal) ? cloudVal->valuedouble : 0.0;

        cJSON *windVal = cJSON_GetArrayItem(winds, i);
        entry->wind_speed_10m = cJSON_IsNumber(windVal) ? windVal->valuedouble : 0.0;

        cJSON *solarVal = cJSON_GetArrayItem(solar, i);
        entry->shortwave_radiation = cJSON_IsNumber(solarVal) ? solarVal->valuedouble : 0.0;

        parsedCount++;
    }

    response->count = parsedCount;

    cJSON_Delete(root);
    LOG_INFO("Parsed %d Open-Meteo entries", parsedCount);
    return 0;
}

int Parser_ParseElpriset(Parser *parser, const char *jsonData, ElprisetResponse *response)
{
    if (!parser || !parser->isInitialized || !jsonData || !response)
    {
        LOG_ERROR("Invalid parameters for Parser_ParseElpriset");
        return -1;
    }

    cJSON *root = cJSON_Parse(jsonData);
    if (!root)
    {
        LOG_ERROR("Failed to parse Elpriset JSON: %s", cJSON_GetErrorPtr());
        return -1;
    }

    if (!cJSON_IsArray(root))
    {
        LOG_ERROR("Elpriset JSON is not an array");
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

        ElprisetEntry *entry = &response->entries[parsedCount];

        cJSON *timeStart = cJSON_GetObjectItem(item, "time_start");
        if (cJSON_IsString(timeStart))
        {
            strncpy(entry->time_start, timeStart->valuestring, sizeof(entry->time_start) - 1);
            entry->time_start[sizeof(entry->time_start) - 1] = '\0';
        }
        else
        {
            entry->time_start[0] = '\0';
        }

        cJSON *timeEnd = cJSON_GetObjectItem(item, "time_end");
        if (cJSON_IsString(timeEnd))
        {
            strncpy(entry->time_end, timeEnd->valuestring, sizeof(entry->time_end) - 1);
            entry->time_end[sizeof(entry->time_end) - 1] = '\0';
        }
        else
        {
            entry->time_end[0] = '\0';
        }

        cJSON *sekPerKwh = cJSON_GetObjectItem(item, "SEK_per_kWh");
        entry->SEK_per_kWh = cJSON_IsNumber(sekPerKwh) ? sekPerKwh->valuedouble : 0.0;

        cJSON *eurPerKwh = cJSON_GetObjectItem(item, "EUR_per_kWh");
        entry->EUR_per_kWh = cJSON_IsNumber(eurPerKwh) ? eurPerKwh->valuedouble : 0.0;

        cJSON *exr = cJSON_GetObjectItem(item, "EXR");
        entry->EXR = cJSON_IsNumber(exr) ? exr->valuedouble : 0.0;

        parsedCount++;
    }

    response->count = parsedCount;

    cJSON_Delete(root);
    LOG_INFO("Parsed %d Elpriset entries", parsedCount);
    return 0;
}

int Parser_BuildPipelineData(const OpenMeteoResponse *weather, const ElprisetResponse *prices, PipelineData *pipeline)
{
    if (!weather || !prices || !pipeline)
    {
        LOG_ERROR("Invalid parameters for Parser_BuildPipelineData");
        return -1;
    }

    int count = weather->count < prices->count ? weather->count : prices->count;
    if (count > 96)
        count = 96;

    int builtCount = 0;

    for (int i = 0; i < count; i++)
    {
        const OpenMeteoEntry *w = &weather->entries[i];
        const ElprisetEntry *p = &prices->entries[i];
        PipelineEntry *entry = &pipeline->entries[builtCount];

        // Parse timestamp from Open-Meteo time string
        struct tm tm = {0};
        if (sscanf(w->time, "%d-%d-%dT%d:%d",
                  &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                  &tm.tm_hour, &tm.tm_min) == 5)
        {
            tm.tm_year -= 1900;
            tm.tm_mon -= 1;
            entry->timestamp = mktime(&tm);
        }
        else
        {
            entry->timestamp = time(NULL);
        }

        entry->solarIrradiance = w->shortwave_radiation;
        entry->cloudCover = w->cloud_cover;
        entry->temperature = w->temperature_2m;
        entry->windSpeed = w->wind_speed_10m;
        entry->humidity = w->humidity_2m;
        entry->spotPriceSek = p->SEK_per_kWh;
        entry->valid = true;

        builtCount++;
    }

    pipeline->count = builtCount;
    pipeline->lastUpdated = time(NULL);

    LOG_INFO("Built PipelineData with %d entries", builtCount);
    return 0;
}

void Parser_Shutdown(Parser *parser)
{
    if (!parser || !parser->isInitialized)
        return;

    parser->isInitialized = false;
    LOG_INFO("Parser shutdown");
}
