#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#include <pthread.h>
#include <stdbool.h>

#include "../../config/config.h"
#include "ThreadWorker.h"

typedef struct
{
    ThreadWorker *threadWorkers;
    pthread_mutex_t mutex;
    int numOfThreads;
    bool isRunning;
    
} ThreadPool;

int ThreadPool_Initiate(ThreadPool *threadPool, int numOfThreads);
int ThreadPool_AddClient(ThreadPool *threadPool, int clientFd);
int ThreadPool_Shutdown(ThreadPool *threadPool);

#endif // _THREADPOOL_H_