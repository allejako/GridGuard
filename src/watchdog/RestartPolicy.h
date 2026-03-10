#ifndef RESTART_POLICY_H
#define RESTART_POLICY_H

// Rate limiting for process restarts with exponential backoff
#define MAX_RESTARTS        5
#define RESTART_WINDOW_SEC  300
#define BASE_BACKOFF_SEC    2

typedef struct RestartPolicy RestartPolicy;

RestartPolicy *RestartPolicy_Create(int max_restarts, int window_sec, int base_backoff_sec);
void           RestartPolicy_Destroy(RestartPolicy *rp);

// Check if we can restart (haven't exceeded rate limit)
int            RestartPolicy_CanRestart(RestartPolicy *rp);
void           RestartPolicy_RecordRestart(RestartPolicy *rp);

// Exponential backoff: 2s, 4s, 8s, 16s, 32s...
int            RestartPolicy_GetBackoffDelay(const RestartPolicy *rp);
int            RestartPolicy_GetCount(const RestartPolicy *rp);
int            RestartPolicy_GetMax(const RestartPolicy *rp);

#endif
