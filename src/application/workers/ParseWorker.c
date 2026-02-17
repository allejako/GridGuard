#include "ParseWorker.h"
#include "FetchWorker.h"
#include "GridGuard.h"
#include "Queue.h"
#include "Parser.h"
#include "Logger.h"
#include "OpenMeteoData.h"
#include "ElprisetData.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Helper function to parse ISO 8601 timestamp to time_t
static time_t ParseISO8601(const char *timeStr)
{
    struct tm tm = {0};
    // Parse "2026-02-09T00:00" or "2026-02-09T00:00:00+01:00"
    sscanf(timeStr, "%d-%d-%dT%d:%d:%d",
           &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
           &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    return mktime(&tm);
}

// Combine parsed weather and price data into unified ForecastData
static void CombineForecastData(const OpenMeteoResponse *weather,
                                const ElprisetResponse *prices,
                                ForecastData *forecast)
{
    memset(forecast, 0, sizeof(ForecastData));
    forecast->lastUpdated = time(NULL);

    // Create entries based on weather data (primary source)
    int count = 0;
    for (int i = 0; i < weather->count && count < 96; i++)
    {
        const OpenMeteoEntry *w = &weather->entries[i];
        ForecastEntry *entry = &forecast->entries[count];

        // Parse timestamp
        entry->timestamp = ParseISO8601(w->time);

        // Copy weather data
        entry->solarIrradiance = w->shortwave_radiation;
        entry->cloudCover = w->cloud_cover;
        entry->temperature = w->temperature_2m;
        entry->windSpeed = w->wind_speed_10m;
        entry->humidity = w->humidity_2m;

        // Find matching price entry by timestamp
        entry->spotPriceSek = 0.0;
        for (int j = 0; j < prices->count; j++)
        {
            const ElprisetEntry *p = &prices->entries[j];
            time_t priceTime = ParseISO8601(p->time_start);

            // Match if within same hour
            if (abs((int)difftime(entry->timestamp, priceTime)) < 3600)
            {
                entry->spotPriceSek = p->SEK_per_kWh;
                break;
            }
        }

        entry->valid = true;
        count++;
    }

    forecast->count = count;
    LOG_INFO("Combined %d forecast entries from %d weather + %d price data points",
             count, weather->count, prices->count);
}

void *ParseWorker_Run(void *arg)
{
    GridGuard *app = (GridGuard *)arg;
    LOG_INFO("ParseWorker: Thread started");

    while (app->isRunning)
    {
        QueueItem item;
        if (Queue_Pop(&app->fetchQueue, &item) != 0)
            break;

        if (item.type != DATA_TYPE_API_RESPONSE)
        {
            free(item.data);
            continue;
        }

        FetchResult *fetchData = (FetchResult *)item.data;
        LOG_INFO("ParseWorker: Processing data for %s/%s", fetchData->location, fetchData->region);

        // Allocate result
        ParseResult *result = calloc(1, sizeof(ParseResult));
        if (!result)
        {
            LOG_ERROR("Parse: Failed to allocate result");
            free(item.data);
            continue;
        }

        strncpy(result->location, fetchData->location, sizeof(result->location) - 1);
        strncpy(result->region, fetchData->region, sizeof(result->region) - 1);
        result->clientFd = fetchData->clientFd;

        // Parse weather and prices separately
        OpenMeteoResponse weather = {0};
        ElprisetResponse prices = {0};
        bool weatherParsed = false;
        bool pricesParsed = false;

        if (strlen(fetchData->weatherJson) > 0)
        {
            if (Parser_ParseOpenMeteo(&app->parser, fetchData->weatherJson, &weather) == 0)
            {
                LOG_INFO("ParseWorker: Parsed %d weather entries", weather.count);
                weatherParsed = true;
            }
            else
            {
                LOG_ERROR("ParseWorker: Failed to parse weather data");
            }
        }

        if (strlen(fetchData->priceJson) > 0)
        {
            if (Parser_ParseElpriset(&app->parser, fetchData->priceJson, &prices) == 0)
            {
                LOG_INFO("ParseWorker: Parsed %d price entries", prices.count);
                pricesParsed = true;
            }
            else
            {
                LOG_ERROR("ParseWorker: Failed to parse price data");
            }
        }

        // Combine into unified ForecastData
        if (weatherParsed && pricesParsed)
        {
            CombineForecastData(&weather, &prices, &result->forecastData);
        }
        else
        {
            LOG_WARNING("ParseWorker: Incomplete data - weather: %s, prices: %s",
                        weatherParsed ? "OK" : "MISSING",
                        pricesParsed ? "OK" : "MISSING");
        }

        // Push to compute queue
        if (Queue_Push(&app->parseQueue, result, sizeof(ParseResult), DATA_TYPE_PARSED_DATA) != 0)
        {
            LOG_ERROR("ParseWorker: Failed to push to compute queue");
            free(result);
        }
        else
        {
            free(result);
        }

        free(item.data);
    }

    LOG_INFO("ParseWorker: Thread exiting");
    return NULL;
}
