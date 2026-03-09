#include "sys/Queue.h"
#include "sys/Logger.h"
#include <stdlib.h>
#include <string.h>

int Queue_Initiate(Queue *queue)
{
    if (!queue)
        return -1;

    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->isShutdown = false;

    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->notEmpty, NULL);
    pthread_cond_init(&queue->notFull, NULL);

    LOG_INFO("Queue initiated");
    return 0;
}

int Queue_Push(Queue *queue, void *data, size_t size, int type)
{
    if (!queue || !data)
        return -1;

    pthread_mutex_lock(&queue->mutex);

    // Wait if queue is full
    while (queue->count >= QUEUE_SIZE && !queue->isShutdown)
        pthread_cond_wait(&queue->notFull, &queue->mutex);

    if (queue->isShutdown)
    {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    // Allocate and copy data
    queue->items[queue->tail].data = malloc(size);
    if (!queue->items[queue->tail].data)
    {
        pthread_mutex_unlock(&queue->mutex);
        LOG_ERROR("Queue: Failed to allocate memory for item");
        return -1;
    }

    memcpy(queue->items[queue->tail].data, data, size);
    queue->items[queue->tail].size = size;
    queue->items[queue->tail].type = type;

    queue->tail = (queue->tail + 1) % QUEUE_SIZE;
    queue->count++;

    // Signal that queue is not empty
    pthread_cond_signal(&queue->notEmpty);
    pthread_mutex_unlock(&queue->mutex);

    return 0;
}

int Queue_Pop(Queue *queue, QueueItem *item)
{
    if (!queue || !item)
        return -1;

    pthread_mutex_lock(&queue->mutex);

    // Wait if queue is empty
    while (queue->count == 0 && !queue->isShutdown)
        pthread_cond_wait(&queue->notEmpty, &queue->mutex);

    if (queue->isShutdown && queue->count == 0)
    {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    // Copy item
    *item = queue->items[queue->head];

    queue->head = (queue->head + 1) % QUEUE_SIZE;
    queue->count--;

    // Signal that queue is not full
    pthread_cond_signal(&queue->notFull);
    pthread_mutex_unlock(&queue->mutex);

    return 0;
}

void Queue_Shutdown(Queue *queue)
{
    if (!queue)
        return;

    pthread_mutex_lock(&queue->mutex);
    queue->isShutdown = true;

    // Free all remaining items
    for (int i = 0; i < queue->count; i++)
    {
        int idx = (queue->head + i) % QUEUE_SIZE;
        if (queue->items[idx].data)
        {
            free(queue->items[idx].data);
            queue->items[idx].data = NULL;
        }
    }

    // Wake up all waiting threads
    pthread_cond_broadcast(&queue->notEmpty);
    pthread_cond_broadcast(&queue->notFull);
    pthread_mutex_unlock(&queue->mutex);

    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->notEmpty);
    pthread_cond_destroy(&queue->notFull);

    LOG_INFO("Queue shutdown");
}
