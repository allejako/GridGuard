#ifndef _GRIDGUARD_H_
#define _GRIDGUARD_H_

#include <pthread.h>
#include <stdbool.h>
#include <sys/types.h>
#include "Compute.h"
#include "SharedCache.h"
#include "ClientDB.h"
#include "WorkCompletion.h"
#include "WorkRequest.h"

// GridGuard application core - Process-based IPC architecture

typedef struct GridGuard
{
    // Child process PIDs
    pid_t fetchPid; 
    pid_t parsePid;

    // IPC file descriptors
    int requestPipeFd;   // Write end: HTTP → Fetch, anonym pipe. 

    // IPC paths
    char fifoPath[256];       // Named pipe: Fetch → Parse
    char socketPath[256];     // Unix socket: Parse → Compute

    // Child binary paths (resolved at runtime from /proc/self/exe)
    char fetcherBin[4096];
    char parserBin[4096];

    // Compute worker thread (körs i main process, läser från Unix socket)
    pthread_t computeThread;

    // Shared resources
    Compute compute;              // Compute service (används av compute thread)
    SharedCache weatherCache;     // shm_open/mmap - delad mellan processer
    SharedCache priceCache;       // shm_open/mmap - delad mellan processer
    ClientDB db;                  // Persistent user config

    // Control
    bool isRunning;
    pthread_mutex_t mutex;

} GridGuard;

int GridGuard_Initiate(GridGuard *app);
int GridGuard_SubmitRequest(GridGuard *app, const WorkRequest *request, WorkCompletion *completion);
void GridGuard_Shutdown(GridGuard *app);

#endif // _GRIDGUARD_H_
