#ifndef _FETCH_WORKER_H_
#define _FETCH_WORKER_H_

// Result from fetch worker - passed to parse worker
typedef struct
{
    char weatherJson[32768];
    char priceJson[16384];
    char location[64];
    char region[16];
    int clientFd;
} FetchResult;

// Fetch worker thread function (pthread-compatible) expects GridGuard* as argument
void *FetchWorker_Run(void *arg);

#endif // _FETCH_WORKER_H_
