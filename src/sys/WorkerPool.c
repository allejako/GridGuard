#include "sys/WorkerPool.h"
#include "sys/Logger.h"
#include "domain/Config.h"
#include <stdlib.h>

int WorkerPool_Initiate(WorkerPool *pool, int numWorkers, WorkerThreadFunc threadFunc)
{
    if (!pool || !threadFunc || numWorkers <= 0)
        return -1;

    if (numWorkers > MAX_THREADS)
    {
        LOG_WARNING("WorkerPool: Too many workers (%d), capping at %d", numWorkers, MAX_THREADS);
        numWorkers = MAX_THREADS;
    }

    pool->workers = malloc(sizeof(Worker) * numWorkers);
    if (!pool->workers)
    {
        LOG_FATAL("WorkerPool: Failed to allocate memory for workers");
        return -1;
    }

    pool->numWorkers = numWorkers;
    pool->threadFunc = threadFunc;
    pool->isRunning  = true;
    pthread_mutex_init(&pool->mutex, NULL);

    for (int i = 0; i < numWorkers; i++)
    {
        pool->workers[i].id        = i;
        pool->workers[i].isRunning = false;
        pool->workers[i].context   = NULL;
        pthread_mutex_init(&pool->workers[i].mutex, NULL);
        pthread_cond_init(&pool->workers[i].cond, NULL);
    }

    LOG_INFO("WorkerPool: Initiated with %d workers (threads not started yet)", numWorkers);
    return 0;
}

int WorkerPool_Start(WorkerPool *pool)
{
    if (!pool)
        return -1;

    for (int i = 0; i < pool->numWorkers; i++)
    {
        pool->workers[i].isRunning = true;
        if (pthread_create(&pool->workers[i].thread, NULL,
                           pool->threadFunc, &pool->workers[i]) != 0)
        {
            LOG_FATAL("WorkerPool: Failed to create thread %d", i);

            // Cleanup already-created threads
            for (int j = 0; j < i; j++)
            {
                pool->workers[j].isRunning = false;
                pthread_cond_signal(&pool->workers[j].cond);
                pthread_join(pool->workers[j].thread, NULL);
                pthread_mutex_destroy(&pool->workers[j].mutex);
                pthread_cond_destroy(&pool->workers[j].cond);
            }

            // Cleanup pool-level resources
            pthread_mutex_destroy(&pool->mutex);
            free(pool->workers);
            pool->workers = NULL;

            return -1;
        }
    }

    LOG_INFO("WorkerPool: Started %d worker threads", pool->numWorkers);
    return 0;
}

Worker *WorkerPool_GetWorker(WorkerPool *pool, int workerId)
{
    if (!pool || workerId < 0 || workerId >= pool->numWorkers)
        return NULL;
    return &pool->workers[workerId];
}

int WorkerPool_GetWorkerCount(WorkerPool *pool)
{
    if (!pool)
        return 0;
    return pool->numWorkers;
}

void WorkerPool_Shutdown(WorkerPool *pool)
{
    if (!pool)
        return;

    LOG_INFO("WorkerPool: Shutting down...");
    pool->isRunning = false;

    for (int i = 0; i < pool->numWorkers; i++)
    {
        pthread_mutex_lock(&pool->workers[i].mutex);
        pool->workers[i].isRunning = false;
        pthread_cond_signal(&pool->workers[i].cond);
        pthread_mutex_unlock(&pool->workers[i].mutex);
    }

    for (int i = 0; i < pool->numWorkers; i++)
    {
        pthread_join(pool->workers[i].thread, NULL);
        pthread_mutex_destroy(&pool->workers[i].mutex);
        pthread_cond_destroy(&pool->workers[i].cond);
    }

    pthread_mutex_destroy(&pool->mutex);
    free(pool->workers);

    LOG_INFO("WorkerPool: Shutdown complete");
}
