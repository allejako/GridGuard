#ifndef _CACHE_WORKER_H_
#define _CACHE_WORKER_H_

#include "Energy.h"

// Result from compute worker - passed to cache worker
typedef struct
{
    EnergyData plan;
    char location[64];
    char region[16];
    int clientFd;
} ComputeResult;

// Cache worker thread function (pthread-compatible) expects GridGuard* as argument
void *CacheWorker_Run(void *arg);

#endif // _CACHE_WORKER_H_
