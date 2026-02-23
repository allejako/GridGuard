#ifndef _WORK_COMPLETION_H
#define _WORK_COMPLETION_H

#include <pthread.h>

#define WORK_COMPLETION_BUFFER_SIZE 8192
#define WORK_COMPLETION_TIMEOUT_SEC 30

// One-shot completion primitive: pipeline signals, HTTP worker waits.
// Mirrors the Linux kernel's struct completion pattern.
// Stack-allocated per request in the HTTP worker thread — safe because
// the worker blocks in WorkCompletion_Wait for the full pipeline duration.
typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    char            json[WORK_COMPLETION_BUFFER_SIZE];
    int             done;
    int             error;
} WorkCompletion;

int  WorkCompletion_Initiate(WorkCompletion *wc);
void WorkCompletion_Destroy(WorkCompletion *wc);

// Called by ComputeWorker: copies result JSON and wakes the HTTP worker.
void WorkCompletion_Signal(WorkCompletion *wc, const char *json);

// Called by ComputeWorker on pipeline error.
void WorkCompletion_SignalError(WorkCompletion *wc);

// Called by HTTP worker: blocks until ComputeWorker signals or timeout.
// Returns 0 on success, -1 on error or timeout.
int  WorkCompletion_Wait(WorkCompletion *wc);

#endif // _WORK_COMPLETION_H
