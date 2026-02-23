#ifndef _FETCH_WORKER_H_
#define _FETCH_WORKER_H_

#include "WorkCompletion.h"

typedef struct
{
    char smhiJson[1000000];
    char openMeteoJson[8192];
    char priceJson[16384];
    char location[64];
    char region[16];
    int clientFd;
    WorkCompletion *completion; // Passed through pipeline, signalled by ComputeWorker
} FetchResult;

void *FetchWorker_Run(void *arg);

#endif // _FETCH_WORKER_H_
