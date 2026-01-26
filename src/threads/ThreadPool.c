#include <stdio.h>
#include <stdlib.h>
#include "ThreadPool.h"

int ThreadPool_Initiate(ThreadPool *threadPool, int numOfThreads)
{
    if (numOfThreads > MAX_THREADS)
        numOfThreads = MAX_THREADS;

    if ((threadPool->threadWorkers = malloc(sizeof(ThreadWorker) * numOfThreads)) == NULL)
    {
        fprintf(stderr, "Error: Could not allocate memory for workers\n");
        return -1;
    }

    threadPool->numOfThreads = numOfThreads;
    threadPool->isRunning = true;
    pthread_mutex_init(&threadPool->mutex, NULL);

    for (int i = 0; i < numOfThreads; i++)
    {
        if (ThreadWorker_Init(&threadPool->threadWorkers[i], i) != 0)
        {
            fprintf(stderr, "Failed to initialize worker %d\n", i);
            // Cleanup
            for (int j = 0; j < i; j++)
                ThreadWorker_Shutdown(&threadPool->threadWorkers[j]);
            free(threadPool->threadWorkers);
            return -1;
        }
    }

    printf("ThreadPool: %d workers ready (capacity: %d clients)\n",
           numOfThreads, numOfThreads * MAX_CLIENTS_PER_THREAD);
    return 0;
}

int ThreadPool_AddClient(ThreadPool *threadPool, int clientFd)
{
    pthread_mutex_lock(&threadPool->mutex);

    // Find worker with fewest clients
    int minClients = MAX_CLIENTS_PER_THREAD + 1;
    int target = -1;

    for (int i = 0; i < threadPool->numOfThreads; i++)
    {
        ThreadWorker *threadWorker = &threadPool->threadWorkers[i];
        pthread_mutex_lock(&threadWorker->mutex);
        if (threadWorker->clientCount < minClients)
        {
            minClients = threadWorker->clientCount;
            target = i;
        }
        pthread_mutex_unlock(&threadWorker->mutex);
    }

    if (target < 0 || minClients >= MAX_CLIENTS_PER_THREAD)
    {
        pthread_mutex_unlock(&threadPool->mutex);
        return -1;
    }

    int result = ThreadWorker_AddClient(&threadPool->threadWorkers[target], clientFd);
    pthread_mutex_unlock(&threadPool->mutex);
    return result;
}

int ThreadPool_Shutdown(ThreadPool *threadPool)
{
    printf("ThreadPool: Shutting down...\n");
    threadPool->isRunning = false;

    for (int i = 0; i < threadPool->numOfThreads; i++)
        ThreadWorker_Shutdown(&threadPool->threadWorkers[i]);

    pthread_mutex_destroy(&threadPool->mutex);
    free(threadPool->threadWorkers);

    printf("ThreadPool: Shutdown complete\n");
    return 0;
}