#include "FetchWorker.h"
#include "GridGuard.h"
#include "Queue.h"
#include "Fetcher.h"
#include "APIEndpoints.h"
#include "Config.h"
#include "Logger.h"
#include <stdlib.h>
#include <string.h>

void *FetchWorker_Run(void *arg)
{
    GridGuard *app = (GridGuard *)arg;
    LOG_INFO("FetchWorker: Thread started");

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
        LOG_INFO("FetchWorker: Processing request for %s/%s", request->location, request->region);

        // Allocate result
        FetchResult *result = calloc(1, sizeof(FetchResult));
        if (!result)
        {
            LOG_ERROR("Fetch: Failed to allocate result");
            free(item.data);
            continue;
        }

        strncpy(result->location, request->location, sizeof(result->location) - 1);
        strncpy(result->region, request->region, sizeof(result->region) - 1);
        result->clientFd = request->clientFd;

        // Build URLs
        char weatherUrl[512];
        char priceUrl[256];
        BuildWeatherApiUrl(weatherUrl, sizeof(weatherUrl), WEATHER_LAT, WEATHER_LON);
        BuildSpotPriceApiUrl(priceUrl, sizeof(priceUrl), request->region, NULL);

        // Fetch weather data
        FetchResponse weatherResp;
        if (Fetcher_Fetch(&app->fetcher, weatherUrl, &weatherResp) == 0)
        {
            strncpy(result->weatherJson, weatherResp.data, sizeof(result->weatherJson) - 1);
            Fetcher_FreeResponse(&weatherResp);
            LOG_INFO("FetchWorker: Got weather data (%zu bytes)", strlen(result->weatherJson));
        }
        else
        {
            LOG_ERROR("FetchWorker: Failed to get weather data");
        }

        // Fetch price data
        FetchResponse priceResp;
        if (Fetcher_Fetch(&app->fetcher, priceUrl, &priceResp) == 0)
        {
            strncpy(result->priceJson, priceResp.data, sizeof(result->priceJson) - 1);
            Fetcher_FreeResponse(&priceResp);
            LOG_INFO("FetchWorker: Got price data (%zu bytes)", strlen(result->priceJson));
        }
        else
        {
            LOG_ERROR("FetchWorker: Failed to get price data");
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
