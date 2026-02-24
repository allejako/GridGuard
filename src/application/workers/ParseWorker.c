#include "ParseWorker.h"
#include "FetchWorker.h"
#include "GridGuard.h"
#include "Queue.h"
#include "Parser.h"
#include "Logger.h"
#include "OpenMeteoResponse.h"
#include "ElprisetResponse.h"
#include "Weather.h"
#include "SpotPrice.h"
#include "Forecast.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

// Helper function to parse ISO 8601 timestamp to time_t
static time_t ParseISO8601(const char *timeStr)
{
    struct tm tm = {0};
    // Parse "2026-02-09T00:00" or "2026-02-09T00:00:00+01:00" or "2026-02-18T17:00:00Z"
    sscanf(timeStr, "%d-%d-%dT%d:%d:%d",
           &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
           &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    return mktime(&tm);
}

// Step 1: Build WeatherData from Open-Meteo (single weather source)
// To add a new weather source in the future: implement a similar function
// and map its fields to WeatherEntry (temperature, humidity, cloudCover,
// windSpeed, solarIrradiance). The rest of the pipeline is source-agnostic.
static void WeatherFromOpenMeteo(const OpenMeteoResponse *openmeteo, WeatherData *weather)
{
    memset(weather, 0, sizeof(WeatherData));
    weather->lastUpdated = time(NULL);

    int count = 0;
    for (int i = 0; i < openmeteo->count && count < 96; i++)
    {
        const OpenMeteoEntry *src = &openmeteo->entries[i];
        WeatherEntry *entry = &weather->entries[count];

        entry->timestamp       = ParseISO8601(src->time);
        entry->temperature     = src->temperature_2m;
        entry->humidity        = src->humidity_2m;
        entry->cloudCover      = src->cloud_cover;    // already %, no octas conversion
        entry->windSpeed       = src->wind_speed_10m;
        entry->solarIrradiance = src->shortwave_radiation;
        entry->valid           = true;
        count++;
    }

    weather->count = count;
    LOG_INFO("ParseWorker: Built weather data from Open-Meteo: %d entries", count);
}

// Step 1.5: Convert ElprisetResponse to SpotPrice domain model
static void ConvertToSpotPrice(const ElprisetResponse *elpriset, const char *region, SpotPrice *spotPrice)
{
    memset(spotPrice, 0, sizeof(SpotPrice));
    spotPrice->lastUpdated = time(NULL);
    strncpy(spotPrice->primarySource, "Elpriset.se", sizeof(spotPrice->primarySource) - 1);

    int count = 0;
    for (int i = 0; i < elpriset->count && count < 96; i++)
    {
        const ElprisetEntry *e = &elpriset->entries[i];
        SpotPriceEntry *entry = &spotPrice->entries[count];

        entry->timestamp = ParseISO8601(e->time_start);
        entry->pricePerKwh = e->SEK_per_kWh;
        strncpy(entry->currency, "SEK", sizeof(entry->currency));
        entry->currency[sizeof(entry->currency) - 1] = '\0';
        strncpy(entry->region, region, sizeof(entry->region));
        entry->region[sizeof(entry->region) - 1] = '\0';
        strncpy(entry->source, "Elpriset.se", sizeof(entry->source));
        entry->source[sizeof(entry->source) - 1] = '\0';
        entry->valid = true;

        count++;
    }

    spotPrice->count = count;
    LOG_INFO("Converted %d spot prices from Elpriset.se to SpotPrice domain model", count);
}

// Step 2: Combine WeatherData + SpotPrice into ForecastData
static void CombineForecastData(const WeatherData *weather, const SpotPrice *spotPrice, ForecastData *forecast)
{
    memset(forecast, 0, sizeof(ForecastData));
    forecast->lastUpdated = time(NULL);

    int count = 0;

    // Use weather data as base
    for (int i = 0; i < weather->count && count < 96; i++)
    {
        const WeatherEntry *w = &weather->entries[i];
        ForecastEntry *entry = &forecast->entries[count];

        // Copy weather data to forecast
        entry->timestamp = w->timestamp;
        entry->temperature = w->temperature;
        entry->humidity = w->humidity;
        entry->cloudCover = w->cloudCover;
        entry->windSpeed = w->windSpeed;
        entry->solarIrradiance = w->solarIrradiance;

        // Find matching price entry
        entry->spotPriceSek = 0.0;  // Default
        for (int j = 0; j < spotPrice->count; j++)
        {
            const SpotPriceEntry *p = &spotPrice->entries[j];

            // Match if exact same hour (60 sec tolerance for clock skew only)
            if (abs((int)difftime(entry->timestamp, p->timestamp)) < 60)
            {
                entry->spotPriceSek = p->pricePerKwh;
                break;
            }
        }

        entry->valid = true;
        count++;
    }

    forecast->count = count;
    LOG_INFO("Combined forecast: %d entries (Weather=%d, SpotPrices=%d)", count, weather->count, spotPrice->count);
}

void *ParseWorker_Run(void *arg)
{
    GridGuard *app = (GridGuard *)arg;
    LOG_INFO("ParseWorker: Thread started (Open-Meteo weather mode)");

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
        result->clientFd        = fetchData->clientFd;
        result->solarAreaM2     = fetchData->solarAreaM2;
        result->solarEfficiency = fetchData->solarEfficiency;
        result->consumptionKwh  = fetchData->consumptionKwh;
        result->completion      = fetchData->completion;

        // Parse all sources
        OpenMeteoResponse omData = {0};
        ElprisetResponse ElprisetPrices = {0};

        bool omParsed = false;
        bool pricesParsed = false;

        // Parse Open-Meteo (weather + solar irradiance)
        if (strlen(fetchData->openMeteoJson) > 0)
        {
            if (Parser_ParseOpenMeteo(&app->parser, fetchData->openMeteoJson, &omData) == 0)
            {
                LOG_INFO("ParseWorker: Parsed %d Open-Meteo entries", omData.count);
                omParsed = true;
            }
            else
            {
                LOG_ERROR("ParseWorker: Open-Meteo parse failed");
            }
        }
        else
        {
            LOG_WARNING("ParseWorker: No Open-Meteo data available");
        }

        // Parse prices
        if (strlen(fetchData->priceJson) > 0)
        {
            if (Parser_ParseElpriset(&app->parser, fetchData->priceJson, &ElprisetPrices) == 0)
            {
                LOG_INFO("ParseWorker: Parsed %d price entries", ElprisetPrices.count);
                pricesParsed = true;
            }
            else
            {
                LOG_ERROR("ParseWorker: Elpriset parse failed");
            }
        }

        // Step 1: Build WeatherData from Open-Meteo
        WeatherData weatherData = {0};
        if (omParsed)
        {
            WeatherFromOpenMeteo(&omData, &weatherData);
        }
        else
        {
            LOG_ERROR("ParseWorker: Cannot create weather data without Open-Meteo!");
        }

        // Step 1.5: Convert ElprisetResponse to SpotPrice domain model
        SpotPrice spotPrice = {0};
        if (pricesParsed)
        {
            ConvertToSpotPrice(&ElprisetPrices, fetchData->region, &spotPrice);
        }
        else
        {
            LOG_WARNING("ParseWorker: Missing spot price data!");
        }

        // Step 2: Combine WeatherData + SpotPrice into ForecastData
        if (weatherData.count > 0)
        {
            CombineForecastData(&weatherData, &spotPrice, &result->forecastData);
        }
        else
        {
            LOG_ERROR("ParseWorker: Cannot create forecast without weather data!");
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
