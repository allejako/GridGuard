#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#include <pthread.h>
#include <stdbool.h>

#include "Config.h"

// Forward declaration (to avoid circular dependency)
struct Pipeline;

// WORKER (Internal)

typedef enum {
    CLIENT_DISCONNECTED = 0,
    CLIENT_CONNECTED,
    CLIENT_AUTHENTICATING,
    CLIENT_READY,
    CLIENT_PROCESSING
} ClientState;

typedef struct {
    int fd;
    ClientState state;
    char buffer[CLIENT_BUFFER_SIZE];
    int bufferLen;
} Client;

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

// THREAD POOL

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