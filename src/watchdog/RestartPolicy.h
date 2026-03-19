#ifndef _RESTART_POLICY_H_
#define _RESTART_POLICY_H_

#include <time.h>

// Rate limiting for process restarts with exponential backoff
#define MAX_RESTARTS        5
#define RESTART_WINDOW_SEC  300
#define BASE_BACKOFF_SEC    2

typedef struct
{
    int    maxRestarts;
    int    windowSec;
    int    baseBackoffSec;
    int    count;
    time_t firstRestart;
    time_t timestamps[MAX_RESTARTS];
} RestartPolicy;

int  RestartPolicy_Initiate(RestartPolicy *rp, int maxRestarts, int windowSec, int baseBackoffSec);
void RestartPolicy_Shutdown(RestartPolicy *rp);

// Check if we can restart (haven't exceeded rate limit)
int  RestartPolicy_CanRestart(RestartPolicy *rp);
void RestartPolicy_RecordRestart(RestartPolicy *rp);

// Exponential backoff: 2s, 4s, 8s, 16s, 32s...
int  RestartPolicy_GetBackoffDelay(const RestartPolicy *rp);
int  RestartPolicy_GetCount(const RestartPolicy *rp);
int  RestartPolicy_GetMax(const RestartPolicy *rp);

#endif // _RESTART_POLICY_H_
