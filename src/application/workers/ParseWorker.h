#ifndef _PARSE_WORKER_H_
#define _PARSE_WORKER_H_

#include "Forecast.h"
#include "WorkCompletion.h"

// Result from parse worker - passed to compute worker
typedef struct
{
    ForecastData    forecastData;
    char            userId[64];  // from JWT claims.subject
    char            location[64]; // user-friendly display name/city
    char            region[16];
    int             clientFd;
    double          solarAreaM2;
    double          solarEfficiency;
    double          consumptionKwh;
    WorkCompletion *completion; // Passed through pipeline, signalled by ComputeWorker
} ParseResult;

// Parse worker thread function (pthread-compatible) expects GridGuard* as argument
void *ParseWorker_Run(void *arg);

#endif // _PARSE_WORKER_H_
