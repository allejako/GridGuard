#ifndef _GRIDGUARD_H_
#define _GRIDGUARD_H_

#include <pthread.h>
#include <stdbool.h>
#include <sys/types.h>
#include "compute/Compute.h"
#include "cache/SharedCache.h"
#include "db/ClientDB.h"
#include "sys/WorkCompletion.h"
#include "ipc/WorkRequest.h"

// GridGuard application core - Process-based IPC architecture

typedef struct GridGuard
{
    // IPC file descriptors
    int requestPipeFd;   // Write end to request FIFO: HTTP → Fetch

    // IPC paths
    char fifoPath[256];       // Named pipe: Fetch → Parse
    char socketPath[256];     // Unix socket: Parse → Compute

    // Compute worker thread (körs i main process, läser från Unix socket)
    pthread_t computeThread;
    void *computeWorker;  // Pointer to ComputeWorker struct (opaque to avoid circular include)

    // Shared resources
    Compute compute;              // Compute service (används av compute thread)
    SharedCache weatherCache;     // shm_open/mmap - delad mellan processer
    SharedCache priceCache;       // shm_open/mmap - delad mellan processer
    SharedCache forecastCache;    // shm_open/mmap - cache for complete forecast JSON
    ClientDB db;                  // Persistent user config

    // Cache invalidation tracking (shared across all worker threads)
    time_t lastDataUpdateCheck;   // Timestamp of last checked data update
    pthread_mutex_t updateCheckMutex;  // Protects lastDataUpdateCheck

    // Control
    bool isRunning;
    pthread_mutex_t mutex;

} GridGuard;

int GridGuard_Initiate(GridGuard *app);
int GridGuard_SubmitRequest(GridGuard *app, const WorkRequest *request, WorkCompletion *completion);
void GridGuard_Shutdown(GridGuard *app);

#endif // _GRIDGUARD_H_
