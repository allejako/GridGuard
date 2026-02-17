#include "GridGuard.h"
#include "FetchWorker.h"
#include "ParseWorker.h"
#include "ComputeWorker.h"
#include "CacheWorker.h"
#include "Queue.h"
#include "Logger.h"

int GridGuard_Initiate(GridGuard *app)
{
    if (!app)
        return -1;

    LOG_INFO("GridGuard: Initiating application core...");

    // Initialize queues
    if (Queue_Initiate(&app->requestQueue) != 0)
    {
        LOG_ERROR("GridGuard: Failed to initiate request queue");
        return -1;
    }
    if (Queue_Initiate(&app->fetchQueue) != 0)
    {
        LOG_ERROR("GridGuard: Failed to initiate fetch queue");
        Queue_Shutdown(&app->requestQueue);
        return -1;
    }
    if (Queue_Initiate(&app->parseQueue) != 0)
    {
        LOG_ERROR("GridGuard: Failed to initiate parse queue");
        Queue_Shutdown(&app->requestQueue);
        Queue_Shutdown(&app->fetchQueue);
        return -1;
    }
    if (Queue_Initiate(&app->computeQueue) != 0)
    {
        LOG_ERROR("GridGuard: Failed to initiate compute queue");
        Queue_Shutdown(&app->requestQueue);
        Queue_Shutdown(&app->fetchQueue);
        Queue_Shutdown(&app->parseQueue);
        return -1;
    }

    // Initialize services/components 
    if (Fetcher_Initiate(&app->fetcher) != 0)
    {
        LOG_ERROR("GridGuard: Failed to initiate APIFetcher");
        Queue_Shutdown(&app->requestQueue);
        Queue_Shutdown(&app->fetchQueue);
        Queue_Shutdown(&app->parseQueue);
        return -1;
    }

    if (Parser_Initiate(&app->parser) != 0)
    {
        LOG_ERROR("GridGuard: Failed to initiate Parser");
        Fetcher_Shutdown(&app->fetcher);
        Queue_Shutdown(&app->requestQueue);
        Queue_Shutdown(&app->fetchQueue);
        Queue_Shutdown(&app->parseQueue);
        return -1;
    }

    // Configure compute with default settings
    // TODO: Replace with client config file
    SolarConfig solar = {
        .panelEfficiency = 0.18,
        .panelAreaM2 = 20.0,
        .orientationDegrees = 180.0,
        .tiltDegrees = 35.0,
        .peakPowerKw = 3.6
    };

    BatteryConfig battery = {
        .capacityKwh = 10.0,
        .maxChargeRateKw = 5.0,
        .maxDischargeRateKw = 5.0,
        .minSocPercent = 20.0,
        .maxSocPercent = 95.0,
        .currentSocPercent = 50.0,
        .efficiency = 0.9
    };

    ConsumptionProfile consumption = {
        .baseLoadKw = 0.5,
        .peakLoadKw = 3.0,
        .averageDailyKwh = 15.0
    };

    // Initialize Compute
    if (Compute_Initiate(&app->compute, &solar, &battery, &consumption) != 0)
    {
        LOG_ERROR("GridGuard: Failed to initiate Compute");
        Parser_Shutdown(&app->parser);
        Fetcher_Shutdown(&app->fetcher);
        Queue_Shutdown(&app->requestQueue);
        Queue_Shutdown(&app->fetchQueue);
        Queue_Shutdown(&app->parseQueue);
        Queue_Shutdown(&app->computeQueue);
        return -1;
    }

    // Initialize Cache with 15 minute TTL
    if (Cache_Initiate(&app->cache, CACHE_DEFAULT_TTL_SECONDS) != 0)
    {
        LOG_ERROR("GridGuard: Failed to initiate Cache");
        Compute_Shutdown(&app->compute);
        Parser_Shutdown(&app->parser);
        Fetcher_Shutdown(&app->fetcher);
        Queue_Shutdown(&app->requestQueue);
        Queue_Shutdown(&app->fetchQueue);
        Queue_Shutdown(&app->parseQueue);
        Queue_Shutdown(&app->computeQueue);
        return -1;
    }

    app->isRunning = true;
    pthread_mutex_init(&app->mutex, NULL);

    // Start worker threads
    if (pthread_create(&app->fetchThread, NULL, FetchWorker_Run, app) != 0)
    {
        LOG_ERROR("GridGuard: Failed to create fetch worker thread");
        Compute_Shutdown(&app->compute);
        Parser_Shutdown(&app->parser);
        Fetcher_Shutdown(&app->fetcher);
        Queue_Shutdown(&app->requestQueue);
        Queue_Shutdown(&app->fetchQueue);
        Queue_Shutdown(&app->parseQueue);
        return -1;
    }

    if (pthread_create(&app->parseThread, NULL, ParseWorker_Run, app) != 0)
    {
        LOG_ERROR("GridGuard: Failed to create parse worker thread");
        app->isRunning = false;
        pthread_join(app->fetchThread, NULL);
        Compute_Shutdown(&app->compute);
        Parser_Shutdown(&app->parser);
        Fetcher_Shutdown(&app->fetcher);
        Queue_Shutdown(&app->requestQueue);
        Queue_Shutdown(&app->fetchQueue);
        Queue_Shutdown(&app->parseQueue);
        return -1;
    }

    if (pthread_create(&app->computeThread, NULL, ComputeWorker_Run, app) != 0)
    {
        LOG_ERROR("GridGuard: Failed to create compute worker thread");
        app->isRunning = false;
        pthread_join(app->fetchThread, NULL);
        pthread_join(app->parseThread, NULL);
        Cache_Shutdown(&app->cache);
        Compute_Shutdown(&app->compute);
        Parser_Shutdown(&app->parser);
        Fetcher_Shutdown(&app->fetcher);
        Queue_Shutdown(&app->requestQueue);
        Queue_Shutdown(&app->fetchQueue);
        Queue_Shutdown(&app->parseQueue);
        Queue_Shutdown(&app->computeQueue);
        return -1;
    }

    if (pthread_create(&app->cacheThread, NULL, CacheWorker_Run, app) != 0)
    {
        LOG_ERROR("GridGuard: Failed to create cache worker thread");
        app->isRunning = false;
        pthread_join(app->fetchThread, NULL);
        pthread_join(app->parseThread, NULL);
        pthread_join(app->computeThread, NULL);
        Cache_Shutdown(&app->cache);
        Compute_Shutdown(&app->compute);
        Parser_Shutdown(&app->parser);
        Fetcher_Shutdown(&app->fetcher);
        Queue_Shutdown(&app->requestQueue);
        Queue_Shutdown(&app->fetchQueue);
        Queue_Shutdown(&app->parseQueue);
        Queue_Shutdown(&app->computeQueue);
        return -1;
    }

    LOG_INFO("GridGuard: Application core initiated with 4 worker threads");
    return 0;
}

int GridGuard_SubmitRequest(GridGuard *app, const WorkRequest *request)
{
    if (!app || !request)
        return -1;

    LOG_INFO("GridGuard: Submitting work request for %s/%s from client FD %d",
             request->location, request->region, request->clientFd);

    return Queue_Push(&app->requestQueue, (void *)request, sizeof(WorkRequest), DATA_TYPE_REQUEST);
}

void GridGuard_Shutdown(GridGuard *app)
{
    if (!app)
        return;

    LOG_INFO("GridGuard: Shutting down application core...");

    app->isRunning = false;

    // Shutdown queues to wake up waiting threads
    Queue_Shutdown(&app->requestQueue);
    Queue_Shutdown(&app->fetchQueue);
    Queue_Shutdown(&app->parseQueue);
    Queue_Shutdown(&app->computeQueue);

    // Wait for all threads to finish
    pthread_join(app->fetchThread, NULL);
    pthread_join(app->parseThread, NULL);
    pthread_join(app->computeThread, NULL);
    pthread_join(app->cacheThread, NULL);

    // Cleanup components
    Cache_Shutdown(&app->cache);
    Compute_Shutdown(&app->compute);
    Parser_Shutdown(&app->parser);
    Fetcher_Shutdown(&app->fetcher);

    pthread_mutex_destroy(&app->mutex);

    LOG_INFO("GridGuard: Application core shutdown complete");
}
