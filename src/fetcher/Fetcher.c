#define _POSIX_C_SOURCE 200809L

#include "fetcher/Fetcher.h"
#include "net/HTTPFetcher.h"
#include "cache/SharedCache.h"
#include "api/APIEndpoints.h"
#include "sys/Logger.h"
#include "ipc/WorkRequest.h"
#include "ipc/FetchResult.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <stdio.h>

int FetcherProcess_Initiate(FetcherProcess *proc, const char *fifoPath)
{
    if (!proc || !fifoPath) 
        return -1;

    memset(proc, 0, sizeof(FetcherProcess));
    strncpy(proc->fifoPath, fifoPath, sizeof(proc->fifoPath) - 1);

    // Allokera HTTPFetcher service
    proc->fetcher = calloc(1, sizeof(HTTPFetcher));
    if (!proc->fetcher)
    {
        LOG_ERROR("FetcherProcess: Failed to allocate HTTPFetcher");
        return -1;
    }

    if (HTTPFetcher_Initiate((HTTPFetcher *)proc->fetcher) != 0)
    {
        LOG_ERROR("FetcherProcess: Failed to initiate HTTPFetcher");
        free(proc->fetcher);
        return -1;
    }

    // Öppna shared memory caches (skapade av main process)
    proc->weatherCache = calloc(1, sizeof(SharedCache));
    proc->priceCache = calloc(1, sizeof(SharedCache));

    if (!proc->weatherCache || !proc->priceCache)
    {
        LOG_ERROR("FetcherProcess: Failed to allocate cache structures");
        HTTPFetcher_Shutdown((HTTPFetcher *)proc->fetcher);
        free(proc->fetcher);
        free(proc->weatherCache);
        free(proc->priceCache);
        return -1;
    }

    // Attach till befintliga shared memory segments
    if (SharedCache_Create((SharedCache *)proc->weatherCache, "/gridguard_weather", 900) != 0)
    {
        LOG_ERROR("FetcherProcess: Failed to attach to weather cache");
        HTTPFetcher_Shutdown((HTTPFetcher *)proc->fetcher);
        free(proc->fetcher);
        free(proc->weatherCache);
        free(proc->priceCache);
        return -1;
    }

    if (SharedCache_Create((SharedCache *)proc->priceCache, "/gridguard_price", 900) != 0)
    {
        LOG_ERROR("FetcherProcess: Failed to attach to price cache");
        SharedCache_Destroy((SharedCache *)proc->weatherCache);
        HTTPFetcher_Shutdown((HTTPFetcher *)proc->fetcher);
        free(proc->fetcher);
        free(proc->weatherCache);
        free(proc->priceCache);
        return -1;
    }

    // Öppna FIFO för skrivning (blockerar tills parse-processen öppnar read end)
    proc->fifoFd = open(fifoPath, O_WRONLY);
    if (proc->fifoFd < 0)
    {
        LOG_ERROR("FetcherProcess: Failed to open FIFO %s for writing", fifoPath);
        SharedCache_Destroy((SharedCache *)proc->priceCache);
        SharedCache_Destroy((SharedCache *)proc->weatherCache);
        HTTPFetcher_Shutdown((HTTPFetcher *)proc->fetcher);
        free(proc->fetcher);
        free(proc->weatherCache);
        free(proc->priceCache);
        return -1;
    }

    proc->stdinFd = STDIN_FILENO;
    proc->isRunning = true;

    LOG_INFO("FetcherProcess: Initialized (PID %d, FIFO %s)", getpid(), fifoPath);
    return 0;
}

int FetcherProcess_Run(FetcherProcess *proc)
{
    if (!proc || !proc->isRunning)
        return -1;

    HTTPFetcher *fetcher = (HTTPFetcher *)proc->fetcher;
    SharedCache *weatherCache = (SharedCache *)proc->weatherCache;
    SharedCache *priceCache = (SharedCache *)proc->priceCache;

    LOG_INFO("FetcherProcess: Starting main loop");

    while (proc->isRunning)
    {
        WorkRequest request;

        // Läs WorkRequest från stdin, pipe kommer från main process i GridGuard.c 
        ssize_t bytesRead = read(proc->stdinFd, &request, sizeof(request));

        if (bytesRead == 0)
        {
            LOG_INFO("FetcherProcess: stdin closed, exiting");
            break;
        }

        if (bytesRead != sizeof(request))
        {
            LOG_ERROR("FetcherProcess: Partial read from stdin (%zd bytes)", bytesRead);
            continue;
        }

        LOG_INFO("FetcherProcess: Processing request for %s/%s", request.userId, request.region);

        // Förbered resultat
        FetchResult result = {0};
        strncpy(result.userId, request.userId, sizeof(result.userId) - 1);
        strncpy(result.location, request.location, sizeof(result.location) - 1);
        strncpy(result.region, request.region, sizeof(result.region) - 1);
        result.solarAreaM2 = request.solarAreaM2;
        result.solarEfficiency = request.solarEfficiency;
        result.consumptionKwh = request.consumptionKwh;
        result.gridFee_low = request.gridFee_low;
        result.gridFee_normal = request.gridFee_normal;
        result.gridFee_high = request.gridFee_high;

        // Hämta väderdata med caching
        char weatherKey[256];
        snprintf(weatherKey, sizeof(weatherKey), "openmeteo_%s_%s", request.lat, request.lon);

        if (SharedCache_Lookup(weatherCache, weatherKey, result.openMeteoJson, sizeof(result.openMeteoJson)) == 0)
        {
            LOG_INFO("FetcherProcess: Weather cache HIT (%s)", weatherKey);
        }
        else
        {
            char openMeteoUrl[512];
            if (BuildOpenMeteoApiUrl(openMeteoUrl, sizeof(openMeteoUrl), request.lat, request.lon) == 0)
            {
                HTTPFetchResponse omResp;
                if (HTTPFetcher_Fetch(fetcher, openMeteoUrl, &omResp) == 0)
                {
                    strncpy(result.openMeteoJson, omResp.data, sizeof(result.openMeteoJson) - 1);
                    SharedCache_Store(weatherCache, weatherKey, omResp.data);
                    HTTPFetcher_FreeResponse(&omResp);
                    LOG_INFO("FetcherProcess: Fetched Open-Meteo data (%zu bytes)", strlen(result.openMeteoJson));
                }
                else
                {
                    LOG_WARNING("FetcherProcess: Open-Meteo fetch failed");
                }
            }
        }

        // Hämta price data
        char priceKey[256];
        time_t now = time(NULL);
        struct tm today;
        localtime_r(&now, &today);
        snprintf(priceKey, sizeof(priceKey), "%s_%04d-%02d-%02d",
                 request.region, today.tm_year + 1900, today.tm_mon + 1, today.tm_mday);

        if (SharedCache_Lookup(priceCache, priceKey,
                              result.priceJson, sizeof(result.priceJson)) == 0)
        {
            LOG_INFO("FetcherProcess: Price cache HIT (%s)", priceKey);
        }
        else
        {
            char priceUrl[256];
            if (BuildSpotPriceApiUrl(priceUrl, sizeof(priceUrl), request.region, NULL) == 0)
            {
                HTTPFetchResponse priceResp;
                if (HTTPFetcher_Fetch(fetcher, priceUrl, &priceResp) == 0)
                {
                    strncpy(result.priceJson, priceResp.data, sizeof(result.priceJson) - 1);
                    SharedCache_Store(priceCache, priceKey, priceResp.data);
                    HTTPFetcher_FreeResponse(&priceResp);
                    LOG_INFO("FetcherProcess: Fetched price data (%zu bytes)", strlen(result.priceJson));
                }
                else
                {
                    LOG_WARNING("FetcherProcess: Price fetch failed");
                }
            }
        }

        // Skriv FetchResult till FIFO (Fetch -> Parse)
        // Vecka 4: Named pipe write
        ssize_t written = write(proc->fifoFd, &result, sizeof(result));
        if (written != sizeof(result))
        {
            LOG_ERROR("FetcherProcess: Failed to write to FIFO (%zd bytes)", written);
            break;
        }

        LOG_INFO("FetcherProcess: Wrote FetchResult to FIFO (%zd bytes)", written);
    }

    LOG_INFO("FetcherProcess: Main loop exited");
    return 0;
}

void FetcherProcess_Shutdown(FetcherProcess *proc)
{
    if (!proc)
        return;

    LOG_INFO("FetcherProcess: Shutting down");

    proc->isRunning = false;

    if (proc->fifoFd >= 0)
        close(proc->fifoFd);

    if (proc->priceCache)
    {
        SharedCache_Destroy((SharedCache *)proc->priceCache);
        free(proc->priceCache);
    }

    if (proc->weatherCache)
    {
        SharedCache_Destroy((SharedCache *)proc->weatherCache);
        free(proc->weatherCache);
    }

    if (proc->fetcher)
    {
        HTTPFetcher_Shutdown((HTTPFetcher *)proc->fetcher);
        free(proc->fetcher);
    }

    LOG_INFO("FetcherProcess: Shutdown complete");
}
