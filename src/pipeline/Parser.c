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

int Parser_BuildForecast(const OpenMeteoResponse *weather, const ElprisetResponse *prices, ForecastData *forecast)
{
    if (!weather || !prices || !forecast)
    {
        LOG_ERROR("Invalid parameters for Parser_BuildForecast");
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
        ForecastEntry *entry = &forecast->entries[builtCount];

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

    forecast->count = builtCount;
    forecast->lastUpdated = time(NULL);

    LOG_INFO("Built ForecastData with %d entries", builtCount);
    return 0;
}

void Parser_Shutdown(Parser *parser)
{
    if (!parser || !parser->isInitialized)
        return;

    parser->isInitialized = false;
    LOG_INFO("Parser shutdown");
}
