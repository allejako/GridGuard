#ifndef COMPUTE_WORKER_H
#define COMPUTE_WORKER_H

#include <stdbool.h>

// ComputeWorker: runs as a thread in the main process.
// Reads ParseResult from the Unix domain socket served by the Parser process,
// generates an energy plan, and signals the waiting HTTP worker thread via
// CompletionRegistry + WorkCompletion.
typedef struct
{
    const char *socketPath; // Unix socket path served by the Parser process
    void       *compute;    // Compute service (opaque pointer to Compute)
    bool        isRunning;
} ComputeWorker;

void *ComputeWorker_Run(void *arg);

#endif // COMPUTE_WORKER_H
