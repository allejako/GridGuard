#ifndef _PIPELINE_THREADS_H_
#define _PIPELINE_THREADS_H_

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include "Fetcher.h"
#include "Parser.h"
#include "Compute.h"

// QUEUE (Internal) 
#define QUEUE_SIZE 100

// Generic queue item that can hold different types of data
typedef struct
{
    void *data;
    size_t size;
    int type;
} QueueItem;

typedef struct
{
    QueueItem items[QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t notEmpty;
    pthread_cond_t notFull;
    bool isShutdown;
} Queue;

// Data types for queue items
typedef enum
{
    DATA_TYPE_REQUEST = 1,
    DATA_TYPE_API_RESPONSE,
    DATA_TYPE_PARSED_DATA,
    DATA_TYPE_ENERGY_PLAN
} DataType;

// Pipeline request from client
typedef struct
{
    int clientFd;
    char location[64];
    char region[16];
} PipelineRequest;

// Multi-threaded pipeline system
typedef struct
{
    // Worker threads
    pthread_t fetchThread;
    pthread_t parseThread;
    pthread_t computeThread;

    // Producer-consumer queues
    Queue requestQueue;  // Client requests → Fetch
    Queue fetchQueue;    // Fetch → Parse
    Queue parseQueue;    // Parse → Compute

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



#endif // _PIPELINE_THREADS_H_
