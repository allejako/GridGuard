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

    cJSON *list = cJSON_GetObjectItem(root, "list");
    if (!cJSON_IsArray(list))
    {
        LOG_ERROR("Weather JSON missing 'list' array");
        cJSON_Delete(root);
        return -1;
    }

    int arraySize = cJSON_GetArraySize(list);
    int parsedCount = 0;

    for (int i = 0; i < arraySize && parsedCount < 96; i++)
    {
        cJSON *item = cJSON_GetArrayItem(list, i);
        if (!item)
            continue;

        WeatherData *data = &forecast->forecasts[parsedCount];

        // Parse timestamp
        cJSON *dt = cJSON_GetObjectItem(item, "dt");
        if (cJSON_IsNumber(dt))
            data->timestamp = (time_t)dt->valueint;
        else
            data->timestamp = time(NULL);

        // Parse main weather data
        cJSON *main = cJSON_GetObjectItem(item, "main");
        if (cJSON_IsObject(main))
        {
            cJSON *temp = cJSON_GetObjectItem(main, "temp");
            cJSON *humidity = cJSON_GetObjectItem(main, "humidity");

            data->temperature = cJSON_IsNumber(temp) ? temp->valuedouble : 0.0;
            data->humidity = cJSON_IsNumber(humidity) ? humidity->valuedouble : 0.0;
        }

        // Parse clouds
        cJSON *clouds = cJSON_GetObjectItem(item, "clouds");
        if (cJSON_IsObject(clouds))
        {
            cJSON *all = cJSON_GetObjectItem(clouds, "all");
            data->cloudCover = cJSON_IsNumber(all) ? all->valuedouble : 0.0;
        }

        // Parse wind
        cJSON *wind = cJSON_GetObjectItem(item, "wind");
        if (cJSON_IsObject(wind))
        {
            cJSON *speed = cJSON_GetObjectItem(wind, "speed");
            data->windSpeed = cJSON_IsNumber(speed) ? speed->valuedouble : 0.0;
        }

        // Calculate solar irradiance based on cloud cover
        // Simple estimation: max 1000 W/m² on clear day, reduced by cloud cover
        double clearSkyIrradiance = 1000.0;
        data->solarIrradiance = clearSkyIrradiance * (1.0 - (data->cloudCover / 100.0));

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

            // Interpolate to 15-minute intervals (copy same price 4 times)
            for (int j = 1; j < 4 && parsedCount < 96; j++)
            {
                SpotPrice *interpolated = &spotData->prices[parsedCount];
                *interpolated = *price;
                interpolated->timestamp += (j * 900); // 900 seconds = 15 minutes
                parsedCount++;
            }
        }
        else
        {
            LOG_WARNING("Invalid spot price at index %d, skipping", i);
        }
    }

    spotData->count = parsedCount;
    spotData->lastUpdated = time(NULL);

    cJSON_Delete(root);
    LOG_INFO("Parsed %d spot prices (interpolated to 15-min intervals)", parsedCount);
    return 0;
}

void Parser_Shutdown(Parser *parser)
{
    if (!parser || !parser->isInitialized)
        return;

    parser->isInitialized = false;
    LOG_INFO("Parser shutdown");
}
