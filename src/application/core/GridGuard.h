#ifndef _GRIDGUARD_H_
#define _GRIDGUARD_H_

#include <pthread.h>
#include <stdbool.h>
#include <sys/types.h>
#include "Compute.h"
#include "SharedCache.h"
#include "Database.h"
#include "WorkCompletion.h"

// Work request submitted by an HTTP worker thread.
// I hybrid mode skickas detta via anonymous pipe till Fetch-processen.
typedef struct
{
    char userId[64];
    char location[64];
    char lat[16];
    char lon[16];
    char region[16];
    double solarAreaM2;
    double solarEfficiency;
    double consumptionKwh;
    double gridFee_low;
    double gridFee_normal;
    double gridFee_high;
} WorkRequest;

// GridGuard application core - Process-based IPC architecture
// Demonstrerar alla IPC-mekanismer från kursen:
// - fork() + exec() + waitpid()
// - Anonymous pipes (HTTP → Fetch)
// - Named pipes/FIFO (Fetch → Parse)
// - Unix domain sockets (Parse → Compute)
// - Shared memory (shm_open/mmap)
// - POSIX semaphores
// - Mutex + condition variables
typedef struct GridGuard
{
    // Child process PIDs
    pid_t fetchPid;
    pid_t parsePid;

    // IPC file descriptors
    int requestPipeFd;   // Write end: HTTP → Fetch (anonymous pipe)

    // IPC paths
    char fifoPath[256];       // Named pipe: Fetch → Parse
    char socketPath[256];     // Unix socket: Parse → Compute

    // Compute worker thread (körs i main process, läser från Unix socket)
    pthread_t computeThread;

    // Shared resources
    Compute compute;              // Compute service (används av compute thread)
    SharedCache weatherCache;     // shm_open/mmap - delad mellan processer
    SharedCache priceCache;       // shm_open/mmap - delad mellan processer
    Database db;                  // Persistent user config

    // Control
    bool isRunning;
    pthread_mutex_t mutex;

} GridGuard;

int GridGuard_Initiate(GridGuard *app);
int GridGuard_SubmitRequest(GridGuard *app, const WorkRequest *request, WorkCompletion *completion);
void GridGuard_Shutdown(GridGuard *app);

#endif // _GRIDGUARD_H_
