#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <unistd.h>

#include "ThreadPool.h"
#include "ClientHandler.h"
#include "Logger.h"
#include "Config.h"

// Each worker pulls an fd from the shared work queue and handles the
// full HTTP request lifecycle: parse → route → respond → close.
// No select(), no state machine, no per-worker client list.
static void *ThreadWorker_Work(void *arg)
{
    Worker             *worker = (Worker *)arg;
    WorkerSharedContext *ctx   = (WorkerSharedContext *)worker->context;

    LOG_INFO("HTTP Worker %d: started", worker->id);

    while (worker->isRunning)
    {
        QueueItem item;
        if (Queue_Pop(ctx->workQueue, &item) != 0)
            break; // Queue shut down — exit cleanly

        int fd = *(int *)item.data;
        free(item.data);

        Client_HandleRequest(fd, ctx->app);
    }

    LOG_INFO("HTTP Worker %d: exiting", worker->id);
    return NULL;
}

int ThreadPool_Initiate(ThreadPool *threadPool, int numOfThreads, struct GridGuard *app)
{
    if (!threadPool || !app || numOfThreads <= 0)
        return -1;

    threadPool->app = app;

    if (Queue_Initiate(&threadPool->workQueue) != 0)
    {
        LOG_FATAL("ThreadPool: Failed to initiate work queue");
        return -1;
    }

    threadPool->sharedCtx.workQueue = &threadPool->workQueue;
    threadPool->sharedCtx.app       = app;

    if (WorkerPool_Initiate(&threadPool->pool, numOfThreads, ThreadWorker_Work) != 0)
    {
        LOG_FATAL("ThreadPool: Failed to initiate worker pool");
        Queue_Shutdown(&threadPool->workQueue);
        return -1;
    }

    // All workers share the same context — the work queue is the load balancer.
    for (int i = 0; i < numOfThreads; i++)
    {
        Worker *worker = WorkerPool_GetWorker(&threadPool->pool, i);
        if (worker)
            worker->context = &threadPool->sharedCtx;
    }

    if (WorkerPool_Start(&threadPool->pool) != 0)
    {
        LOG_FATAL("ThreadPool: Failed to start worker threads");
        Queue_Shutdown(&threadPool->workQueue);
        return -1;
    }

    LOG_INFO("ThreadPool: %d HTTP workers ready (work-queue model)", numOfThreads);
    return 0;
}

int ThreadPool_AddClient(ThreadPool *threadPool, int clientFd)
{
    if (!threadPool || clientFd < 0)
        return -1;

    // Push fd into work queue — next available worker picks it up.
    return Queue_Push(&threadPool->workQueue, &clientFd, sizeof(int), DATA_TYPE_REQUEST);
}

int ThreadPool_Shutdown(ThreadPool *threadPool)
{
    if (!threadPool)
        return -1;

    LOG_INFO("ThreadPool: Shutting down...");

    // Wake all workers blocked on Queue_Pop.
    Queue_Shutdown(&threadPool->workQueue);

    // Join all worker threads.
    WorkerPool_Shutdown(&threadPool->pool);

    LOG_INFO("ThreadPool: Shutdown complete");
    return 0;
}
