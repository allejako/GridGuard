#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <errno.h>

#include "ThreadWorker.h"

// ============== STATE MACHINE ==============
static void Client_HandleState(Client *client)
{
    switch (client->state)
    {
    case CLIENT_CONNECTED:
        // TODO
        client->state = CLIENT_READY;
        break;

    case CLIENT_READY:
        // TODO
        printf("  [Client fd=%d]: %s", client->fd, client->buffer);
        
        // Echo
        send(client->fd, client->buffer, client->bufferLen, 0);
        break;

    case CLIENT_PROCESSING:
        // TODO
        break;

    default:
        break;
    }
}

// ============== WORKER THREAD LOOP ==============
static void *ThreadWorker_Run(void *arg)
{
    ThreadWorker *worker = (ThreadWorker *)arg;
    printf("Worker %d: Started\n", worker->id);

    while (worker->isRunning)
    {
        fd_set readFds;
        int maxFd = -1;
        struct timeval timeout = {.tv_sec = 1, .tv_usec = 0};

        pthread_mutex_lock(&worker->mutex);

        // Wait if no clients
        while (worker->clientCount == 0 && worker->isRunning)
            pthread_cond_wait(&worker->cond, &worker->mutex);

        if (!worker->isRunning)
        {
            pthread_mutex_unlock(&worker->mutex);
            break;
        }

        // Build fd_set
        FD_ZERO(&readFds);
        for (int i = 0; i < MAX_CLIENTS_PER_THREAD; i++)
        {
            if (worker->clients[i].state != CLIENT_DISCONNECTED)
            {
                FD_SET(worker->clients[i].fd, &readFds);
                if (worker->clients[i].fd > maxFd)
                    maxFd = worker->clients[i].fd;
            }
        }

        pthread_mutex_unlock(&worker->mutex);

        if (maxFd < 0)
            continue;

        // Wait for activity
        int ready = select(maxFd + 1, &readFds, NULL, NULL, &timeout);
        if (ready <= 0)
            continue;

        // Handle readable clients
        pthread_mutex_lock(&worker->mutex);
        for (int i = 0; i < MAX_CLIENTS_PER_THREAD; i++)
        {
            Client *client = &worker->clients[i];
            if (client->state == CLIENT_DISCONNECTED)
                continue;

            if (FD_ISSET(client->fd, &readFds))
            {
                ssize_t bytes = recv(client->fd, client->buffer, CLIENT_BUFFER_SIZE - 1, 0);

                if (bytes <= 0)
                {
                    // Disconnected
                    printf("Worker %d: Client fd=%d disconnected\n", worker->id, client->fd);
                    close(client->fd);
                    client->state = CLIENT_DISCONNECTED;
                    client->fd = -1;
                    worker->clientCount--;
                }
                else
                {
                    client->buffer[bytes] = '\0';
                    client->bufferLen = bytes;
                    Client_HandleState(client);
                }
            }
        }
        pthread_mutex_unlock(&worker->mutex);
    }

    // Cleanup
    pthread_mutex_lock(&worker->mutex);
    for (int i = 0; i < MAX_CLIENTS_PER_THREAD; i++)
    {
        if (worker->clients[i].state != CLIENT_DISCONNECTED)
        {
            close(worker->clients[i].fd);
            worker->clients[i].state = CLIENT_DISCONNECTED;
        }
    }
    pthread_mutex_unlock(&worker->mutex);

    printf("Worker %d: Exiting\n", worker->id);
    return NULL;
}

// ============== PUBLIC API ==============
int ThreadWorker_Init(ThreadWorker *worker, int id)
{
    worker->id = id;
    worker->clientCount = 0;
    worker->isRunning = true;

    // Initialize all client slots as empty
    for (int i = 0; i < MAX_CLIENTS_PER_THREAD; i++)
    {
        worker->clients[i].fd = -1;
        worker->clients[i].state = CLIENT_DISCONNECTED;
        worker->clients[i].bufferLen = 0;
    }

    pthread_mutex_init(&worker->mutex, NULL);
    pthread_cond_init(&worker->cond, NULL);

    if (pthread_create(&worker->thread, NULL, ThreadWorker_Run, worker) != 0)
    {
        perror("pthread_create");
        return -1;
    }

    return 0;
}

int ThreadWorker_AddClient(ThreadWorker *worker, int clientFd)
{
    pthread_mutex_lock(&worker->mutex);

    // Find empty slot
    for (int i = 0; i < MAX_CLIENTS_PER_THREAD; i++)
    {
        if (worker->clients[i].state == CLIENT_DISCONNECTED)
        {
            worker->clients[i].fd = clientFd;
            worker->clients[i].state = CLIENT_CONNECTED;
            worker->clients[i].bufferLen = 0;
            worker->clientCount++;

            printf("Worker %d: Added client fd=%d (slot %d, total %d)\n",
                   worker->id, clientFd, i, worker->clientCount);

            pthread_cond_signal(&worker->cond);
            pthread_mutex_unlock(&worker->mutex);
            return 0;
        }
    }

    pthread_mutex_unlock(&worker->mutex);
    return -1; // Full
}

void ThreadWorker_Shutdown(ThreadWorker *worker)
{
    pthread_mutex_lock(&worker->mutex);
    worker->isRunning = false;
    pthread_cond_signal(&worker->cond);
    pthread_mutex_unlock(&worker->mutex);

    pthread_join(worker->thread, NULL);
    pthread_mutex_destroy(&worker->mutex);
    pthread_cond_destroy(&worker->cond);
}