#ifndef _PIPELINE_ORCHESTRATOR_H_
#define _PIPELINE_ORCHESTRATOR_H_

#include <pthread.h>
#include <stdbool.h>
#include "Queue.h"
#include "Fetcher.h"
#include "Parser.h"
#include "Compute.h"

// Pipeline request from client
typedef struct
{
    int clientFd;
    char location[64];
    char region[16];
} PipelineRequest;

// Multi-threaded pipeline system
typedef struct Pipeline
{
    // Worker threads
    pthread_t fetchThread;
    pthread_t parseThread;
    pthread_t computeThread;

    // Producer-consumer queues
    Queue requestQueue;  // Client requests -> Fetch
    Queue fetchQueue;    // Fetch -> Parse
    Queue parseQueue;    // Parse -> Compute

    // Pipeline components
    Fetcher fetcher;
    Parser parser;
    Compute compute;

    // Control
    bool isRunning;
    pthread_mutex_t mutex;

} Pipeline;

int Pipeline_Initiate(Pipeline *pipeline);
int Pipeline_SubmitRequest(Pipeline *pipeline, const PipelineRequest *request);
void Pipeline_Shutdown(Pipeline *pipeline);

#endif // _PIPELINE_ORCHESTRATOR_H_
