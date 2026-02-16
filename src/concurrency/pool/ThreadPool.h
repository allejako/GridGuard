#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#include <pthread.h>
#include <stdbool.h>

#include "Config.h"
#include "ClientHandler.h"

// Forward declaration (to avoid circular dependency)
struct Pipeline;

// Thread worker - manages a set of clients
typedef struct {
    int id;
    pthread_t thread;
    Client clients[MAX_CLIENTS_PER_THREAD];
    int clientCount;
    bool isRunning;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    struct Pipeline *pipeline;  // Reference to pipeline (not owned)
} ThreadWorker;

// Thread pool - manages multiple workers
typedef struct
{
    ThreadWorker *threadWorkers;
    pthread_mutex_t mutex;
    int numOfThreads;
    bool isRunning;
    struct Pipeline *pipeline;  // Reference to pipeline (not owned)

} ThreadPool;

int ThreadPool_Initiate(ThreadPool *threadPool, int numOfThreads, struct Pipeline *pipeline);
int ThreadPool_AddClient(ThreadPool *threadPool, int clientFd);
int ThreadPool_Shutdown(ThreadPool *threadPool);

#endif // _THREADPOOL_H_
