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
    LOG_INFO("FetchWorker: Thread started (multi-source mode: SMHI + Open-Meteo)");

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

        // Build URLs for all sources
        char smhiUrl[512];
        char openMeteoUrl[512];
        char priceUrl[256];

        BuildSMHIApiUrl(smhiUrl, sizeof(smhiUrl), WEATHER_LAT, WEATHER_LON);
        BuildOpenMeteoApiUrl(openMeteoUrl, sizeof(openMeteoUrl), WEATHER_LAT, WEATHER_LON);
        BuildSpotPriceApiUrl(priceUrl, sizeof(priceUrl), request->region, NULL);

        // Fetch from SMHI (primary source for Swedish weather)
        FetchResponse smhiResp;
        if (Fetcher_Fetch(&app->fetcher, smhiUrl, &smhiResp) == 0)
        {
            strncpy(result->smhiJson, smhiResp.data, sizeof(result->smhiJson) - 1);
            Fetcher_FreeResponse(&smhiResp);
            LOG_INFO("FetchWorker: Got SMHI data (%zu bytes)", strlen(result->smhiJson));
        }
        else
        {
            LOG_WARNING("FetchWorker: SMHI fetch failed - will use Open-Meteo as fallback");
        }

        // Fetch from Open-Meteo (backup + solar irradiance data)
        FetchResponse omResp;
        if (Fetcher_Fetch(&app->fetcher, openMeteoUrl, &omResp) == 0)
        {
            strncpy(result->openMeteoJson, omResp.data, sizeof(result->openMeteoJson) - 1);
            // Backward compatibility: also store in weatherJson
            strncpy(result->weatherJson, omResp.data, sizeof(result->weatherJson) - 1);
            Fetcher_FreeResponse(&omResp);
            LOG_INFO("FetchWorker: Got Open-Meteo data (%zu bytes)", strlen(result->openMeteoJson));
        }
        else
        {
            LOG_WARNING("FetchWorker: Open-Meteo fetch failed");
        }

        // Check if we got at least one weather source
        if (strlen(result->smhiJson) == 0 && strlen(result->openMeteoJson) == 0)
        {
            LOG_ERROR("FetchWorker: CRITICAL - Both weather sources failed!");
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
