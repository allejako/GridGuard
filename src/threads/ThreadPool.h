#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#include <pthread.h>
#include <stdbool.h>

#include "ThreadWorker.h"

#define MAX_THREADS 20
#define MAX_CLIENTS_PER_THREAD 50

typedef struct
{
    ThreadWorker *workers;
    pthread_mutex_t mutex;
    bool isRunning;
} ThreadPool;

int ThreadPool_Initiate(ThreadPool *threadPool, int numOfThreads);
int ThreadPool_AddClient(ThreadPool *threadPool, int clientFd);
int ThreadPool_Shutdown(ThreadPool *threadPool);

#endif // _THREADPOOL_H_