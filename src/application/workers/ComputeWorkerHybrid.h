#ifndef _COMPUTE_WORKER_HYBRID_H_
#define _COMPUTE_WORKER_HYBRID_H_

#include <stdbool.h>

// Hybrid Compute worker - körs som tråd i main process
// men läser ParseResult från Unix domain socket istället för Queue.
// Använder CompletionRegistry för att hitta rätt WorkCompletion baserat på userId.

typedef struct
{
    const char *socketPath;  // Path till Unix socket som Parse-processen serverar
    void *compute;           // Compute service (opaque pointer)
    bool isRunning;
} ComputeWorkerHybrid;

void *ComputeWorkerHybrid_Run(void *arg);

#endif // _COMPUTE_WORKER_HYBRID_H_
