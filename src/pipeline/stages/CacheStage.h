#ifndef _CACHE_STAGE_H_
#define _CACHE_STAGE_H_

#include "EnergyData.h"

// Result from compute stage - passed to cache stage
typedef struct
{
    EnergyData plan;
    char location[64];
    char region[16];
    int clientFd;
} ComputeResult;

// Forward declaration
struct Pipeline;

// Cache thread worker function
void *CacheStage_Work(void *arg);

#endif // _CACHE_STAGE_H_
