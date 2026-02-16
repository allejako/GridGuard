#ifndef _FETCH_STAGE_H_
#define _FETCH_STAGE_H_

// Result from fetch stage - passed to parse stage
typedef struct
{
    char weatherJson[8192];
    char priceJson[16384];
    char location[64];
    char region[16];
    int clientFd;
} FetchResult;

// Forward declaration
struct Pipeline;

// Fetch thread worker function
void *FetchStage_Work(void *arg);

#endif // _FETCH_STAGE_H_
