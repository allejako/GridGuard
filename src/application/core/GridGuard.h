#ifndef _GRIDGUARD_H_
#define _GRIDGUARD_H_

#include <pthread.h>
#include <stdbool.h>
#include "Queue.h"
#include "Fetcher.h"
#include "Parser.h"
#include "Compute.h"
#include "Cache.h"

// Work request from client
typedef struct
{
    int clientFd;
    char location[64]; // TEMP LÖSNING
    char region[16]; // TEMP LÖSNING
} WorkRequest;

// Multi-threaded application core
typedef struct GridGuard
{
    // Worker threads
    pthread_t fetchThread;
    pthread_t parseThread;
    pthread_t computeThread;
    pthread_t cacheThread;

    // Producer-consumer queues
    Queue requestQueue;  // Client requests -> Fetch
    Queue fetchQueue;    // Fetch -> Parse
    Queue parseQueue;    // Parse -> Compute
    Queue computeQueue;  // Compute -> Cache

    // Application services
    Fetcher fetcher;
    Parser parser;
    Compute compute;
    Cache cache;

    // Control
    bool isRunning;
    pthread_mutex_t mutex;

} GridGuard;

int GridGuard_Initiate(GridGuard *app);
int GridGuard_SubmitRequest(GridGuard *app, const WorkRequest *request);
void GridGuard_Shutdown(GridGuard *app);

#endif // _GRIDGUARD_H_
