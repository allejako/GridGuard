#include "ParseWorker.h"
#include "FetchWorker.h"
#include "GridGuard.h"
#include "Queue.h"
#include "Parser.h"
#include "Logger.h"
#include "SMHIResponse.h"
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

// Helper to find SMHI parameter by name
static const SMHIParameter* FindSMHIParameter(const SMHITimeEntry *entry, const char *name)
{
    for (int i = 0; i < entry->paramCount; i++)
    {
        if (strcmp(entry->parameters[i].name, name) == 0)
        {
            return &entry->parameters[i];
        }
    }
    return NULL;
}

// Step 1: Combine SMHI + OpenMeteo into WeatherData
static void CombineWeatherData(const SMHIResponse *smhi, const OpenMeteoResponse *openmeteo, WeatherData *weather)
{
    memset(weather, 0, sizeof(WeatherData));
    weather->lastUpdated = time(NULL);

    int count = 0;

    // Primary source: SMHI (use all its time entries)
    for (int i = 0; i < smhi->count && count < 96; i++)
    {
        const SMHITimeEntry *smhiEntry = &smhi->timeSeries[i];
        WeatherEntry *entry = &weather->entries[count];

        // Parse timestamp
        entry->timestamp = ParseISO8601(smhiEntry->validTime);

        // Extract SMHI parameters
        const SMHIParameter *temp = FindSMHIParameter(smhiEntry, "t");
        if (temp) entry->temperature = temp->value;

        const SMHIParameter *humidity = FindSMHIParameter(smhiEntry, "r");
        if (humidity) entry->humidity = humidity->value;

        const SMHIParameter *windSpeed = FindSMHIParameter(smhiEntry, "ws");
        if (windSpeed) entry->windSpeed = windSpeed->value;  // Already in m/s

        const SMHIParameter *cloudCover = FindSMHIParameter(smhiEntry, "tcc_mean");
        if (cloudCover)
        {
            // Convert octas (0-8) to percent (0-100)
            entry->cloudCover = (cloudCover->value / 8.0) * 100.0;
        }

        // Find matching Open-Meteo entry for solar irradiance
        entry->solarIrradiance = 0.0;  // Default if not found
        for (int j = 0; j < openmeteo->count; j++)
        {
            const OpenMeteoEntry *omEntry = &openmeteo->entries[j];
            time_t omTime = ParseISO8601(omEntry->time);

            // Match if exact same hour (60 sec tolerance for clock skew only)
            // Both APIs give hourly data (14:00, 15:00, etc), timestamps should match exactly
            if (abs((int)difftime(entry->timestamp, omTime)) < 60)
            {
                entry->solarIrradiance = omEntry->shortwave_radiation;
                break;
            }
        }

        entry->valid = true;
        count++;
    }

    weather->count = count;
    LOG_INFO("Combined weather data: %d entries (SMHI=%d, OpenMeteo solar=%d)",
             count, smhi->count, openmeteo->count);
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
    LOG_INFO("ParseWorker: Thread started (SMHI + OpenMeteo solar mode)");

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

        // Parse all sources
        SMHIResponse smhiData = {0};
        OpenMeteoResponse omData = {0};
        ElprisetResponse ElprisetPrices = {0};

        bool smhiParsed = false;
        bool omParsed = false;
        bool pricesParsed = false;

        // Parse SMHI (primary weather source)
        if (strlen(fetchData->smhiJson) > 0)
        {
            if (Parser_ParseSMHI(&app->parser, fetchData->smhiJson, &smhiData) == 0)
            {
                LOG_INFO("ParseWorker: Parsed %d SMHI entries", smhiData.count);
                smhiParsed = true;
            }
            else
            {
                LOG_ERROR("ParseWorker: SMHI parse failed");
            }
        }
        else
        {
            LOG_WARNING("ParseWorker: No SMHI data available");
        }

        // Parse Open-Meteo (solar irradiance source)
        if (strlen(fetchData->openMeteoJson) > 0)
        {
            if (Parser_ParseOpenMeteo(&app->parser, fetchData->openMeteoJson, &omData) == 0)
            {
                LOG_INFO("ParseWorker: Parsed %d Open-Meteo entries (solar irradiance)",
                        omData.count);
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

        // Step 1: Combine SMHI + OpenMeteo into WeatherData
        WeatherData weatherData = {0};
        if (smhiParsed)
        {
            if (!omParsed)
            {
                LOG_WARNING("ParseWorker: Missing solar irradiance data from Open-Meteo!");
            }

            CombineWeatherData(&smhiData, &omData, &weatherData);
        }
        else
        {
            LOG_ERROR("ParseWorker: Cannot create weather data without SMHI!");
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
