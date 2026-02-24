#ifndef _FETCH_WORKER_H_
#define _FETCH_WORKER_H_

#include "WorkCompletion.h"

typedef struct
{
    char openMeteoJson[8192];
    char priceJson[16384];
    char userId[64];   // from JWT claims.subject
    char location[64]; // user-friendly display name/city
    char region[16];
    int  clientFd;
    double solarAreaM2;
    double solarEfficiency;
    double consumptionKwh;
    WorkCompletion *completion; // Passed through pipeline, signalled by ComputeWorker
} FetchResult;

void *FetchWorker_Run(void *arg);

#endif // _FETCH_WORKER_H_
