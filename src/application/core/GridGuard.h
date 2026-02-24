#ifndef _GRIDGUARD_H_
#define _GRIDGUARD_H_

#include <pthread.h>
#include <stdbool.h>
#include "Queue.h"
#include "Fetcher.h"
#include "Parser.h"
#include "Compute.h"
#include "JsonCache.h"
#include "WorkCompletion.h"
#include "Database.h"

// Work request submitted by an HTTP worker thread.
// completion points to a WorkCompletion on the HTTP worker's stack —
// valid for the lifetime of the request since the worker blocks on it.
typedef struct
{
    int  clientFd;
    char userId[64];   // from JWT claims.subject
    char location[64]; // user-friendly display name/city
    char lat[16];      // latitude string, e.g. "59.3300"
    char lon[16];              // longitude string, e.g. "18.0700"
    char region[16];          // grid region code, e.g. "SE3"
    double solarAreaM2;      // from UserConfig
    double solarEfficiency;  // from UserConfig
    double consumptionKwh;   // from UserConfig (hourly base load)
    WorkCompletion *completion; // Result delivery back to HTTP worker
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

    // Persistent user configuration
    Database db;

    // Control
    bool isRunning;
    pthread_mutex_t mutex;

} GridGuard;

int GridGuard_Initiate(GridGuard *app);
int GridGuard_SubmitRequest(GridGuard *app, const WorkRequest *request);
void GridGuard_Shutdown(GridGuard *app);

#endif // _GRIDGUARD_H_
