#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <errno.h>

#include "ThreadPool.h"
#include "ClientHandler.h"
#include "Logger.h"

// ========== WORKER IMPLEMENTATION (Internal) ==========

static void *ThreadWorker_Work(void *arg)
{
    Worker *worker = (Worker *)arg;
    ThreadWorkerContext *ctx = (ThreadWorkerContext *)worker->context;
    LOG_INFO("Worker %d: Started", worker->id);

    while (worker->isRunning)
    {
        fd_set readFds;
        int maxFd = -1;
        struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};

        pthread_mutex_lock(&worker->mutex);

        // Wait if no clients
        while (ctx->clientCount == 0 && worker->isRunning)
            pthread_cond_wait(&worker->cond, &worker->mutex);

        if (!worker->isRunning)
        {
            pthread_mutex_unlock(&worker->mutex);
            break;
        }

        // Build fd_set
        FD_ZERO(&readFds);
        for (int i = 0; i < MAX_CLIENTS_PER_THREAD; i++)
        {
            if (ctx->clients[i].state != CLIENT_DISCONNECTED)
            {
                FD_SET(ctx->clients[i].fd, &readFds);
                if (ctx->clients[i].fd > maxFd)
                    maxFd = ctx->clients[i].fd;
            }
        }

        pthread_mutex_unlock(&worker->mutex);

        if (maxFd < 0)
            continue;

        // Wait for activity
        int ready = select(maxFd + 1, &readFds, NULL, NULL, &timeout);
        if (ready <= 0)
            continue;

        // Handle readable clients
        pthread_mutex_lock(&worker->mutex);
        for (int i = 0; i < MAX_CLIENTS_PER_THREAD; i++)
        {
            Client *client = &ctx->clients[i];
            if (client->state == CLIENT_DISCONNECTED)
                continue;

            if (FD_ISSET(client->fd, &readFds))
            {
                ssize_t bytes = recv(client->fd, client->buffer, CLIENT_BUFFER_SIZE - 1, 0);

                if (bytes <= 0)
                {
                    // Disconnected
                    LOG_INFO("Worker %d: Client FD %d disconnected", worker->id, client->fd);
                    close(client->fd);
                    client->state = CLIENT_DISCONNECTED;
                    client->fd = -1;
                    ctx->clientCount--;
                }
                else
                {
                    client->buffer[bytes] = '\0';
                    client->bufferLen = bytes;
                    Client_HandleState(client, ctx->threadPool->app);
                }
            }
        }
        pthread_mutex_unlock(&worker->mutex);
    }

    // Cleanup
    pthread_mutex_lock(&worker->mutex);
    for (int i = 0; i < MAX_CLIENTS_PER_THREAD; i++)
    {
        if (ctx->clients[i].state != CLIENT_DISCONNECTED)
        {
            close(ctx->clients[i].fd);
            ctx->clients[i].state = CLIENT_DISCONNECTED;
        }
    }
    pthread_mutex_unlock(&worker->mutex);

    LOG_INFO("Worker %d: Exiting", worker->id);
    return NULL;
}

// Add client to a specific worker context
static int ThreadWorkerContext_AddClient(ThreadWorkerContext *ctx, Worker *worker, int clientFd)
{
    pthread_mutex_lock(&worker->mutex);

    // Find empty slot
    for (int i = 0; i < MAX_CLIENTS_PER_THREAD; i++)
    {
        if (ctx->clients[i].state == CLIENT_DISCONNECTED)
        {
            ctx->clients[i].fd = clientFd;
            ctx->clients[i].state = CLIENT_CONNECTED;
            ctx->clients[i].bufferLen = 0;
            ctx->clientCount++;

            LOG_INFO("Worker %d: Added client FD %d (slot %d, total %d client(s))",
                   worker->id, clientFd, i, ctx->clientCount);

            pthread_cond_signal(&worker->cond);
            pthread_mutex_unlock(&worker->mutex);
            return 0;
        }
    }

    pthread_mutex_unlock(&worker->mutex);
    return -1;
}

// ========== THREAD POOL IMPLEMENTATION ==========

int ThreadPool_Initiate(ThreadPool *threadPool, int numOfThreads, struct GridGuard *app)
{
    if (!threadPool || !app)
        return -1;

    threadPool->app = app;

    // Allocate context for each worker
    threadPool->contexts = malloc(sizeof(ThreadWorkerContext) * numOfThreads);
    if (!threadPool->contexts)
    {
        LOG_FATAL("ThreadPool: Failed to allocate worker contexts");
        return -1;
    }

    // Initialize each context
    for (int i = 0; i < numOfThreads; i++)
    {
        threadPool->contexts[i].clientCount = 0;
        threadPool->contexts[i].threadPool = threadPool;

        // Initialize all client slots as empty
        for (int j = 0; j < MAX_CLIENTS_PER_THREAD; j++)
        {
            threadPool->contexts[i].clients[j].fd = -1;
            threadPool->contexts[i].clients[j].state = CLIENT_DISCONNECTED;
            threadPool->contexts[i].clients[j].bufferLen = 0;
        }
    }

    // Initialize worker pool with thread function and scheduling policy (doesn't start threads yet)
    if (WorkerPool_Initiate(&threadPool->pool, numOfThreads, ThreadWorker_Work,
                           SCHEDULING_POLICY_LEAST_CONNECTIONS) != 0)
    {
        LOG_FATAL("ThreadPool: Failed to initialize worker pool");
        free(threadPool->contexts);
        return -1;
    }

    // Set context for each worker BEFORE starting threads
    for (int i = 0; i < numOfThreads; i++)
    {
        Worker *worker = WorkerPool_GetWorker(&threadPool->pool, i);
        if (worker)
            worker->context = &threadPool->contexts[i];
    }

    // Now start the worker threads (context is set, safe to access)
    if (WorkerPool_Start(&threadPool->pool) != 0)
    {
        LOG_FATAL("ThreadPool: Failed to start worker threads");
        free(threadPool->contexts);
        return -1;
    }

    LOG_INFO("ThreadPool: Initiated with %d workers (capacity: %d clients)",
             numOfThreads, numOfThreads * MAX_CLIENTS_PER_THREAD);
    return 0;
}

int ThreadPool_AddClient(ThreadPool *threadPool, int clientFd)
{
    if (!threadPool)
        return -1;

    int numWorkers = WorkerPool_GetWorkerCount(&threadPool->pool);

    // Build worker load information for scheduler
    WorkerLoad workerLoads[numWorkers];
    for (int i = 0; i < numWorkers; i++)
    {
        Worker *worker = WorkerPool_GetWorker(&threadPool->pool, i);
        ThreadWorkerContext *ctx = (ThreadWorkerContext *)worker->context;

        pthread_mutex_lock(&worker->mutex);
        workerLoads[i].workerId = i;
        workerLoads[i].currentLoad = ctx->clientCount;
        workerLoads[i].capacity = MAX_CLIENTS_PER_THREAD;
        pthread_mutex_unlock(&worker->mutex);
    }

    // Let scheduler select best worker
    int target = Scheduler_SelectWorker(&threadPool->pool.scheduler, workerLoads, numWorkers);

    if (target < 0)
    {
        LOG_WARNING("ThreadPool: No available worker (all at capacity)");
        return -1;
    }

    Worker *worker = WorkerPool_GetWorker(&threadPool->pool, target);
    ThreadWorkerContext *ctx = (ThreadWorkerContext *)worker->context;

    return ThreadWorkerContext_AddClient(ctx, worker, clientFd);
}

int ThreadPool_Shutdown(ThreadPool *threadPool)
{
    if (!threadPool)
        return -1;

    LOG_INFO("ThreadPool: Shutting down...");

    WorkerPool_Shutdown(&threadPool->pool);
    free(threadPool->contexts);

    LOG_INFO("ThreadPool: Shutdown complete");
    return 0;
}
