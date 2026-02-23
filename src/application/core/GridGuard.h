#ifndef _GRIDGUARD_H_
#define _GRIDGUARD_H_

#include <pthread.h>
#include <stdbool.h>
#include "Queue.h"
#include "Fetcher.h"
#include "Parser.h"
#include "Compute.h"
#include "JsonCache.h"

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

    // Producer-consumer queues
    Queue requestQueue;  // Client requests -> Fetch
    Queue fetchQueue;    // Fetch -> Parse
    Queue parseQueue;    // Parse -> Compute

    // Application services
    Fetcher fetcher;
    Parser parser;
    Compute compute;

    // Fetch-level caches (shared across clients)
    JsonCache weatherCache;
    JsonCache priceCache;

    // Control
    bool isRunning;
    pthread_mutex_t mutex;

} GridGuard;

int GridGuard_Initiate(GridGuard *app);
int GridGuard_SubmitRequest(GridGuard *app, const WorkRequest *request);
void GridGuard_Shutdown(GridGuard *app);

#endif // _GRIDGUARD_H_
