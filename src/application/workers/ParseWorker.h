#ifndef _PARSE_WORKER_H_
#define _PARSE_WORKER_H_

#include "OpenMeteoData.h"
#include "ElprisetData.h"

// Result from parse worker - passed to compute worker
typedef struct
{
    OpenMeteoResponse weather;
    ElprisetResponse prices;
    char location[64];
    char region[16];
    int clientFd;
} ParseResult;

// Parse worker thread function (pthread-compatible) expects GridGuard* as argument
void *ParseWorker_Run(void *arg);

#endif // _PARSE_WORKER_H_
