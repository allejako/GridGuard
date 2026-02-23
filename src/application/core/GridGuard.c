#include "GridGuard.h"
#include "FetchWorker.h"
#include "ParseWorker.h"
#include "ComputeWorker.h"
#include "Queue.h"
#include "Logger.h"
#include "Config.h"

int GridGuard_Initiate(GridGuard *app)
{
    if (!app)
        return -1;

    LOG_INFO("GridGuard: Initiating application core...");

    // Initialize database
    if (Database_Initiate(&app->db, DB_PATH) != 0)
    {
        LOG_ERROR("GridGuard: Failed to initiate database");
        return -1;
    }

    // Initialize queues
    if (Queue_Initiate(&app->requestQueue) != 0)
    {
        LOG_ERROR("GridGuard: Failed to initiate request queue");
        Database_Shutdown(&app->db);
        return -1;
    }
    if (Queue_Initiate(&app->fetchQueue) != 0)
    {
        LOG_ERROR("GridGuard: Failed to initiate fetch queue");
        Queue_Shutdown(&app->requestQueue);
        Database_Shutdown(&app->db);
        return -1;
    }
    if (Queue_Initiate(&app->parseQueue) != 0)
    {
        LOG_ERROR("GridGuard: Failed to initiate parse queue");
        Queue_Shutdown(&app->requestQueue);
        Queue_Shutdown(&app->fetchQueue);
        Database_Shutdown(&app->db);
        return -1;
    }

    // Initialize services/components
    if (Fetcher_Initiate(&app->fetcher) != 0)
    {
        LOG_ERROR("GridGuard: Failed to initiate APIFetcher");
        Queue_Shutdown(&app->requestQueue);
        Queue_Shutdown(&app->fetchQueue);
        Queue_Shutdown(&app->parseQueue);
        Database_Shutdown(&app->db);
        return -1;
    }

    if (Parser_Initiate(&app->parser) != 0)
    {
        LOG_ERROR("GridGuard: Failed to initiate Parser");
        Fetcher_Shutdown(&app->fetcher);
        Queue_Shutdown(&app->requestQueue);
        Queue_Shutdown(&app->fetchQueue);
        Queue_Shutdown(&app->parseQueue);
        Database_Shutdown(&app->db);
        return -1;
    }

    // Initialize Compute
    if (Compute_Initiate(&app->compute) != 0)
    {
        LOG_ERROR("GridGuard: Failed to initiate Compute");
        Parser_Shutdown(&app->parser);
        Fetcher_Shutdown(&app->fetcher);
        Queue_Shutdown(&app->requestQueue);
        Queue_Shutdown(&app->fetchQueue);
        Queue_Shutdown(&app->parseQueue);
        Database_Shutdown(&app->db);
        return -1;
    }

    // Initialize fetch-level caches
    if (JsonCache_Initiate(&app->weatherCache, JSON_CACHE_DEFAULT_TTL_SEC) != 0)
    {
        LOG_ERROR("GridGuard: Failed to initiate weather cache");
        Compute_Shutdown(&app->compute);
        Parser_Shutdown(&app->parser);
        Fetcher_Shutdown(&app->fetcher);
        Queue_Shutdown(&app->requestQueue);
        Queue_Shutdown(&app->fetchQueue);
        Queue_Shutdown(&app->parseQueue);
        Database_Shutdown(&app->db);
        return -1;
    }

    if (JsonCache_Initiate(&app->priceCache, JSON_CACHE_DEFAULT_TTL_SEC) != 0)
    {
        LOG_ERROR("GridGuard: Failed to initiate price cache");
        JsonCache_Shutdown(&app->weatherCache);
        Compute_Shutdown(&app->compute);
        Parser_Shutdown(&app->parser);
        Fetcher_Shutdown(&app->fetcher);
        Queue_Shutdown(&app->requestQueue);
        Queue_Shutdown(&app->fetchQueue);
        Queue_Shutdown(&app->parseQueue);
        Database_Shutdown(&app->db);
        return -1;
    }

    app->isRunning = true;
    pthread_mutex_init(&app->mutex, NULL);

    // Start worker threads
    if (pthread_create(&app->fetchThread, NULL, FetchWorker_Run, app) != 0)
    {
        LOG_ERROR("GridGuard: Failed to create fetch worker thread");
        JsonCache_Shutdown(&app->priceCache);
        JsonCache_Shutdown(&app->weatherCache);
        Compute_Shutdown(&app->compute);
        Parser_Shutdown(&app->parser);
        Fetcher_Shutdown(&app->fetcher);
        Queue_Shutdown(&app->requestQueue);
        Queue_Shutdown(&app->fetchQueue);
        Queue_Shutdown(&app->parseQueue);
        Database_Shutdown(&app->db);
        return -1;
    }

    if (pthread_create(&app->parseThread, NULL, ParseWorker_Run, app) != 0)
    {
        LOG_ERROR("GridGuard: Failed to create parse worker thread");
        app->isRunning = false;
        pthread_join(app->fetchThread, NULL);
        JsonCache_Shutdown(&app->priceCache);
        JsonCache_Shutdown(&app->weatherCache);
        Compute_Shutdown(&app->compute);
        Parser_Shutdown(&app->parser);
        Fetcher_Shutdown(&app->fetcher);
        Queue_Shutdown(&app->requestQueue);
        Queue_Shutdown(&app->fetchQueue);
        Queue_Shutdown(&app->parseQueue);
        Database_Shutdown(&app->db);
        return -1;
    }

    if (pthread_create(&app->computeThread, NULL, ComputeWorker_Run, app) != 0)
    {
        LOG_ERROR("GridGuard: Failed to create compute worker thread");
        app->isRunning = false;
        pthread_join(app->fetchThread, NULL);
        pthread_join(app->parseThread, NULL);
        JsonCache_Shutdown(&app->priceCache);
        JsonCache_Shutdown(&app->weatherCache);
        Compute_Shutdown(&app->compute);
        Parser_Shutdown(&app->parser);
        Fetcher_Shutdown(&app->fetcher);
        Queue_Shutdown(&app->requestQueue);
        Queue_Shutdown(&app->fetchQueue);
        Queue_Shutdown(&app->parseQueue);
        Database_Shutdown(&app->db);
        return -1;
    }

    LOG_INFO("GridGuard: Application core initiated with 3 worker threads");
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

    // Wait for all threads to finish
    pthread_join(app->fetchThread, NULL);
    pthread_join(app->parseThread, NULL);
    pthread_join(app->computeThread, NULL);

    // Cleanup components
    JsonCache_Shutdown(&app->priceCache);
    JsonCache_Shutdown(&app->weatherCache);
    Compute_Shutdown(&app->compute);
    Parser_Shutdown(&app->parser);
    Fetcher_Shutdown(&app->fetcher);
    Database_Shutdown(&app->db);

    pthread_mutex_destroy(&app->mutex);

    LOG_INFO("GridGuard: Application core shutdown complete");
}
