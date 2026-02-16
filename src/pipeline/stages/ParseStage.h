#ifndef _PARSE_STAGE_H_
#define _PARSE_STAGE_H_

#include "OpenMeteoData.h"
#include "ElprisetData.h"

// Result from parse stage - passed to compute stage
typedef struct
{
    OpenMeteoResponse weather;
    ElprisetResponse prices;
    char location[64];
    char region[16];
    int clientFd;
} ParseResult;

// Forward declaration
struct Pipeline;

// Parse thread worker function
void *ParseStage_Work(void *arg);

#endif // _PARSE_STAGE_H_
