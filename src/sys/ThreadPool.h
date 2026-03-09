#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#include "sys/WorkerPool.h"
#include "sys/Queue.h"

struct GridGuard;

// Shared context given to every HTTP worker thread.
// Workers compete for fds from the work queue — no scheduler needed.
typedef struct
{
    Queue          *workQueue;
    struct GridGuard *app;
} WorkerSharedContext;

// Thread pool: N workers pulling HTTP connections from a shared work queue.
// Replaces the old select-loop multiplexing model.
typedef struct ThreadPool
{
    WorkerPool         pool;
    Queue              workQueue;
    WorkerSharedContext sharedCtx;
    struct GridGuard   *app;
} ThreadPool;

int ThreadPool_Initiate(ThreadPool *threadPool, int numOfThreads, struct GridGuard *app);
int ThreadPool_AddClient(ThreadPool *threadPool, int clientFd);
int ThreadPool_Shutdown(ThreadPool *threadPool);

#endif // _THREADPOOL_H_
