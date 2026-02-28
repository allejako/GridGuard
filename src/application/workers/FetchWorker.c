#include "FetchWorker.h"
#include "GridGuard.h"
#include "Queue.h"
#include "Fetcher.h"
#include "APIEndpoints.h"
#include "Config.h"
#include "Logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

void *FetchWorker_Run(void *arg)
{
    GridGuard *app = (GridGuard *)arg;
    LOG_INFO("FetchWorker: Thread started (weather source: Open-Meteo)");

    while (app->isRunning)
    {
        QueueItem item;
        if (Queue_Pop(&app->requestQueue, &item) != 0)
            break;

        if (item.type != DATA_TYPE_REQUEST)
        {
            free(item.data);
            continue;
        }

        WorkRequest *request = (WorkRequest *)item.data;
        LOG_INFO("FetchWorker: Processing request for %s/%s", request->userId, request->region);

        // Allocate result
        FetchResult *result = calloc(1, sizeof(FetchResult));
        if (!result)
        {
            LOG_ERROR("Fetch: Failed to allocate result");
            free(item.data);
            continue;
        }

        strncpy(result->userId, request->userId, sizeof(result->userId) - 1);
        strncpy(result->location, request->location, sizeof(result->location) - 1);
        strncpy(result->region, request->region, sizeof(result->region) - 1);
        result->clientFd        = request->clientFd;
        result->solarAreaM2     = request->solarAreaM2;
        result->solarEfficiency = request->solarEfficiency;
        result->consumptionKwh  = request->consumptionKwh;
        result->completion      = request->completion;

        // --- Open-Meteo weather data: key = "openmeteo_lat_lon" ---
        char weatherKey[SHARED_CACHE_KEY_MAX];
        snprintf(weatherKey, sizeof(weatherKey), "openmeteo_%s_%s", request->lat, request->lon);

        if (SharedCache_Lookup(&app->weatherCache, weatherKey,
                              result->openMeteoJson, sizeof(result->openMeteoJson)) == 0)
        {
            LOG_INFO("FetchWorker: Open-Meteo cache HIT (%s)", weatherKey);
        }
        else
        {
            char openMeteoUrl[512];
            BuildOpenMeteoApiUrl(openMeteoUrl, sizeof(openMeteoUrl), request->lat, request->lon);

            FetchResponse omResp;
            if (Fetcher_Fetch(&app->fetcher, openMeteoUrl, &omResp) == 0)
            {
                strncpy(result->openMeteoJson, omResp.data, sizeof(result->openMeteoJson) - 1);
                SharedCache_Store(&app->weatherCache, weatherKey, omResp.data);
                Fetcher_FreeResponse(&omResp);
                LOG_INFO("FetchWorker: Got Open-Meteo data (%zu bytes)", strlen(result->openMeteoJson));
            }
            else
            {
                LOG_WARNING("FetchWorker: Open-Meteo fetch failed");
            }
        }

        if (strlen(result->openMeteoJson) == 0)
        {
            LOG_ERROR("FetchWorker: CRITICAL - Open-Meteo fetch failed!");
        }

        // --- Price data: key = "region_YYYY-MM-DD" ---
        char priceKey[SHARED_CACHE_KEY_MAX];
        {
            time_t now = time(NULL);
            struct tm today;
            localtime_r(&now, &today);
            snprintf(priceKey, sizeof(priceKey), "%s_%04d-%02d-%02d",
                     request->region,
                     today.tm_year + 1900,
                     today.tm_mon + 1,
                     today.tm_mday);
        }

        if (SharedCache_Lookup(&app->priceCache, priceKey,
                              result->priceJson, sizeof(result->priceJson)) == 0)
        {
            LOG_INFO("FetchWorker: Price cache HIT (%s)", priceKey);
        }
        else
        {
            char priceUrl[256];
            BuildSpotPriceApiUrl(priceUrl, sizeof(priceUrl), request->region, NULL);

            FetchResponse priceResp;
            if (Fetcher_Fetch(&app->fetcher, priceUrl, &priceResp) == 0)
            {
                strncpy(result->priceJson, priceResp.data, sizeof(result->priceJson) - 1);
                SharedCache_Store(&app->priceCache, priceKey, priceResp.data);
                Fetcher_FreeResponse(&priceResp);
                LOG_INFO("FetchWorker: Got price data (%zu bytes)", strlen(result->priceJson));
            }
            else
            {
                LOG_ERROR("FetchWorker: Failed to get price data");
            }
        }

        // Push to parse queue
        if (Queue_Push(&app->fetchQueue, result, sizeof(FetchResult), DATA_TYPE_API_RESPONSE) != 0)
        {
            LOG_ERROR("FetchWorker: Failed to push to parse queue");
            free(result);
        }
        else
        {
            free(result);
        }

        free(item.data);
    }

    LOG_INFO("FetchWorker: Thread exiting");
    return NULL;
}
