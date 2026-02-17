#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#include <pthread.h>
#include <stdbool.h>

#include "Config.h"
#include "ClientHandler.h"
#include "WorkerPool.h"

struct GridGuard;
struct ThreadPool;

// Context for each worker thread - contains client-specific data
typedef struct
{
    Client clients[MAX_CLIENTS_PER_THREAD];
    int clientCount;
    struct ThreadPool *threadPool;   // Reference to parent thread pool for accessing shared resources
} ThreadWorkerContext;

// Thread pool - specialized worker pool for handling network I/O
typedef struct ThreadPool
{
    WorkerPool pool;                  // Generic worker pool (lifecycle + scheduling)
    ThreadWorkerContext *contexts;    // Context for each worker
    struct GridGuard *app;            // Reference to main application for accessing queues and services
} ThreadPool;

int ThreadPool_Initiate(ThreadPool *threadPool, int numOfThreads, struct GridGuard *app);
int ThreadPool_AddClient(ThreadPool *threadPool, int clientFd);
int ThreadPool_Shutdown(ThreadPool *threadPool);

#endif // _THREADPOOL_H_
