#ifndef _QUEUE_H_
#define _QUEUE_H_

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

#define QUEUE_SIZE 100

// Data types for queue items
typedef enum
{
    DATA_TYPE_REQUEST = 1,
    DATA_TYPE_API_RESPONSE,
    DATA_TYPE_PARSED_DATA,
    DATA_TYPE_ENERGY_PLAN
} DataType;

// Generic queue item that can hold different types of data
typedef struct
{
    void *data;
    size_t size;
    int type;
} QueueItem;

// Thread-safe queue with condition variables
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

int Queue_Initiate(Queue *queue);
int Queue_Push(Queue *queue, void *data, size_t size, int type);
int Queue_Pop(Queue *queue, QueueItem *item);
void Queue_Shutdown(Queue *queue);

#endif // _QUEUE_H_
