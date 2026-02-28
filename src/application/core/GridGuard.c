#include "GridGuard.h"
#include "FetchWorker.h"
#include "ParseWorker.h"
#include "ComputeWorker.h"
#include "Queue.h"
#include "Logger.h"
#include "Config.h"

#include <stdlib.h>

int GridGuard_Initiate(GridGuard *app)
{
    if (!app)
        return -1;

    LOG_INFO("GridGuard: Initiating application core...");

    // Initialize database — allow runtime override for daemon environments
    // where the daemon has chdir("/") making relative paths unusable.
    const char *dbPath = getenv("GRIDGUARD_DB_PATH");
    if (!dbPath || dbPath[0] == '\0')
        dbPath = DB_PATH;

    if (Database_Initiate(&app->db, dbPath) != 0)
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

    // Initialize fetch-level caches in POSIX shared memory
    if (SharedCache_Create(&app->weatherCache, "/gridguard_weather", SHARED_CACHE_DEFAULT_TTL) != 0)
    {
        LOG_ERROR("GridGuard: Failed to create weather shared cache");
        Compute_Shutdown(&app->compute);
        Parser_Shutdown(&app->parser);
        Fetcher_Shutdown(&app->fetcher);
        Queue_Shutdown(&app->requestQueue);
        Queue_Shutdown(&app->fetchQueue);
        Queue_Shutdown(&app->parseQueue);
        Database_Shutdown(&app->db);
        return -1;
    }

    if (SharedCache_Create(&app->priceCache, "/gridguard_price", SHARED_CACHE_DEFAULT_TTL) != 0)
    {
        LOG_ERROR("GridGuard: Failed to create price shared cache");
        SharedCache_Destroy(&app->weatherCache);
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
        SharedCache_Destroy(&app->priceCache);
        SharedCache_Destroy(&app->weatherCache);
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
        // Shutdown queues first to unblock fetchThread blocked in Queue_Pop,
        // then join — otherwise pthread_join deadlocks.
        Queue_Shutdown(&app->requestQueue);
        Queue_Shutdown(&app->fetchQueue);
        Queue_Shutdown(&app->parseQueue);
        pthread_join(app->fetchThread, NULL);
        SharedCache_Destroy(&app->priceCache);
        SharedCache_Destroy(&app->weatherCache);
        Compute_Shutdown(&app->compute);
        Parser_Shutdown(&app->parser);
        Fetcher_Shutdown(&app->fetcher);
        Database_Shutdown(&app->db);
        return -1;
    }

    if (pthread_create(&app->computeThread, NULL, ComputeWorker_Run, app) != 0)
    {
        LOG_ERROR("GridGuard: Failed to create compute worker thread");
        app->isRunning = false;
        // Shutdown all queues before joining to unblock threads in Queue_Pop.
        Queue_Shutdown(&app->requestQueue);
        Queue_Shutdown(&app->fetchQueue);
        Queue_Shutdown(&app->parseQueue);
        pthread_join(app->fetchThread, NULL);
        pthread_join(app->parseThread, NULL);
        SharedCache_Destroy(&app->priceCache);
        SharedCache_Destroy(&app->weatherCache);
        Compute_Shutdown(&app->compute);
        Parser_Shutdown(&app->parser);
        Fetcher_Shutdown(&app->fetcher);
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
             request->userId, request->region, request->clientFd);

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
    SharedCache_Destroy(&app->priceCache);
    SharedCache_Destroy(&app->weatherCache);
    Compute_Shutdown(&app->compute);
    Parser_Shutdown(&app->parser);
    Fetcher_Shutdown(&app->fetcher);
    Database_Shutdown(&app->db);

    pthread_mutex_destroy(&app->mutex);

    LOG_INFO("GridGuard: Application core shutdown complete");
}
