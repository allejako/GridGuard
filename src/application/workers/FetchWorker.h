#ifndef _FETCH_WORKER_H_
#define _FETCH_WORKER_H_

// Result from fetch worker - passed to parse worker
typedef struct
{
    char weatherJson[8192];
    char priceJson[16384];
    char location[64];
    char region[16];
    int clientFd;
} FetchResult;

// Forward declaration
struct GridGuard;

// Fetch worker thread function
void *FetchWorker_Run(void *arg);

#endif // _FETCH_WORKER_H_
