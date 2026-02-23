#ifndef _PARSE_WORKER_H_
#define _PARSE_WORKER_H_

#include "Forecast.h"
#include "WorkCompletion.h"

// Result from parse worker - passed to compute worker
typedef struct
{
    ForecastData    forecastData;
    char            location[64];
    char            region[16];
    int             clientFd;
    WorkCompletion *completion; // Passed through pipeline, signalled by ComputeWorker
} ParseResult;

// Parse worker thread function (pthread-compatible) expects GridGuard* as argument
void *ParseWorker_Run(void *arg);

#endif // _PARSE_WORKER_H_
