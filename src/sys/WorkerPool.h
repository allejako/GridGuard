#ifndef _WORKERPOOL_H_
#define _WORKERPOOL_H_

#include <pthread.h>
#include <stdbool.h>

// Generic worker thread function signature
typedef void *(*WorkerThreadFunc)(void *workerContext);

// Generic worker — can be used for any kind of work
typedef struct
{
    int id;
    pthread_t thread;
    bool isRunning;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    void *context; // Worker-specific context (set by caller)
} Worker;

// Worker pool — manages the lifecycle of N worker threads.
// Scheduling strategy is left to the caller (e.g. work queue in ThreadPool).
typedef struct
{
    Worker *workers;
    int numWorkers;
    WorkerThreadFunc threadFunc;
    bool isRunning;
    pthread_mutex_t mutex;
} WorkerPool;

int     WorkerPool_Initiate(WorkerPool *pool, int numWorkers, WorkerThreadFunc threadFunc);
int     WorkerPool_Start(WorkerPool *pool);
Worker *WorkerPool_GetWorker(WorkerPool *pool, int workerId);
int     WorkerPool_GetWorkerCount(WorkerPool *pool);
void    WorkerPool_Shutdown(WorkerPool *pool);

#endif // _WORKERPOOL_H_
