#ifndef _FETCH_WORKER_H_
#define _FETCH_WORKER_H_

typedef struct
{
    char smhiJson[8192];
    char openMeteoJson[8192];
    char weatherJson[32768];
    char priceJson[16384];
    char location[64];
    char region[16];
    int clientFd;
} FetchResult;

void *FetchWorker_Run(void *arg);

#endif // _FETCH_WORKER_H_
