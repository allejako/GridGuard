#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <errno.h>

#include "ThreadPool.h"
#include "PipelineThreads.h"
#include "Logger.h"

// ========== WORKER IMPLEMENTATION (Internal) ==========

static void Client_HandleState(Client *client, struct Pipeline *pipeline)
{
    switch (client->state)
    {
    case CLIENT_CONNECTED:
        // Send welcome message
        const char *welcome = "GridGuard LEOP Server\nCommands: forecast [location] [region]\nExample: forecast stockholm SE3\n\n> ";
        send(client->fd, welcome, strlen(welcome), 0);
        client->state = CLIENT_READY;

        // If buffer has data, process it immediately
        if (client->bufferLen > 0)
        {
            Client_HandleState(client, pipeline);  // Recursive call to handle the command
        }
        break;

    case CLIENT_READY:
        // Parse command from client
        char command[32] = {0};
        char location[64] = "stockholm";  // Default
        char region[16] = "SE3";          // Default

        sscanf(client->buffer, "%31s %63s %15s", command, location, region);

        if (strcmp(command, "forecast") == 0)
        {
            LOG_INFO("Worker: Client FD %d requested forecast for %s/%s", client->fd, location, region);

            // Submit to pipeline
            PipelineRequest request = {
                .clientFd = client->fd
            };
            strncpy(request.location, location, sizeof(request.location) - 1);
            strncpy(request.region, region, sizeof(request.region) - 1);

            if (Pipeline_SubmitRequest((Pipeline *)pipeline, &request) == 0)
            {
                const char *processing = "Processing request...\n";
                send(client->fd, processing, strlen(processing), 0);
                client->state = CLIENT_PROCESSING;
            }
            else
            {
                const char *error = "ERROR: Pipeline queue full, try again later\n> ";
                send(client->fd, error, strlen(error), 0);
            }
        }
        else if (strcmp(command, "help") == 0 || strlen(client->buffer) == 0)
        {
            const char *help =
                "Available commands:\n"
                "  forecast [location] [region] - Get energy forecast\n"
                "  help                         - Show this help\n"
                "\n> ";
            send(client->fd, help, strlen(help), 0);
        }
        else
        {
            const char *unknown = "Unknown command. Type 'help' for available commands\n> ";
            send(client->fd, unknown, strlen(unknown), 0);
        }
        break;

    case CLIENT_PROCESSING:
        // Client is waiting for pipeline response
        // Response will be sent directly from pipeline
        // Set back to READY state after a moment
        client->state = CLIENT_READY;
        break;

    default:
        break;
    }
}

static void *ThreadWorker_Work(void *arg)
{
    ThreadWorker *worker = (ThreadWorker *)arg;
    LOG_INFO("Worker %d: Started", worker->id);

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
                    LOG_INFO("Worker %d: Client FD %d disconnected", worker->id, client->fd);
                    close(client->fd);
                    client->state = CLIENT_DISCONNECTED;
                    client->fd = -1;
                    worker->clientCount--;
                }
                else
                {
                    client->buffer[bytes] = '\0';
                    client->bufferLen = bytes;
                    Client_HandleState(client, worker->pipeline);
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

    LOG_INFO("Worker %d: Exiting", worker->id);
    return NULL;
}

static int ThreadWorker_Initiate(ThreadWorker *worker, int id, struct Pipeline *pipeline)
{
    worker->id = id;
    worker->clientCount = 0;
    worker->isRunning = true;
    worker->pipeline = pipeline;

    // Initialize all client slots as empty
    for (int i = 0; i < MAX_CLIENTS_PER_THREAD; i++)
    {
        worker->clients[i].fd = -1;
        worker->clients[i].state = CLIENT_DISCONNECTED;
        worker->clients[i].bufferLen = 0;
    }

    pthread_mutex_init(&worker->mutex, NULL);
    pthread_cond_init(&worker->cond, NULL);

    if (pthread_create(&worker->thread, NULL, ThreadWorker_Work, worker) != 0)
    {
        LOG_ERROR("Failed creating thread %d", worker->id);
        return -1;
    }

    return 0;
}

static int ThreadWorker_AddClient(ThreadWorker *worker, int clientFd)
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

            LOG_INFO("Worker %d: Added client FD %d (slot %d, total %d client(s))",
                   worker->id, clientFd, i, worker->clientCount);

            pthread_cond_signal(&worker->cond);
            pthread_mutex_unlock(&worker->mutex);
            return 0;
        }
    }

    pthread_mutex_unlock(&worker->mutex);
    return -1;
}

static void ThreadWorker_Shutdown(ThreadWorker *worker)
{
    pthread_mutex_lock(&worker->mutex);
    worker->isRunning = false;
    pthread_cond_signal(&worker->cond);
    pthread_mutex_unlock(&worker->mutex);

    pthread_join(worker->thread, NULL);
    pthread_mutex_destroy(&worker->mutex);
    pthread_cond_destroy(&worker->cond);
}

// ========== THREAD POOL IMPLEMENTATION ==========

int ThreadPool_Initiate(ThreadPool *threadPool, int numOfThreads, struct Pipeline *pipeline)
{
    if (numOfThreads > MAX_THREADS)
    {
        LOG_WARNING("Too many threads. Automatically reduced to max amount (%d)", MAX_THREADS);
        numOfThreads = MAX_THREADS;
    }

    if ((threadPool->threadWorkers = malloc(sizeof(ThreadWorker) * numOfThreads)) == NULL)
    {
        LOG_FATAL("Failed to allocate memory for thread workers");
        return -1;
    }

    threadPool->numOfThreads = numOfThreads;
    threadPool->isRunning = true;
    threadPool->pipeline = pipeline;
    pthread_mutex_init(&threadPool->mutex, NULL);

    for (int i = 0; i < numOfThreads; i++)
    {
        if (ThreadWorker_Initiate(&threadPool->threadWorkers[i], i, pipeline) != 0)
        {
            LOG_FATAL("Failed to initialize thread workers");
            for (int j = 0; j < i; j++)
                ThreadWorker_Shutdown(&threadPool->threadWorkers[j]);
            free(threadPool->threadWorkers);
            return EXIT_FAILURE;
        }
    }

    LOG_INFO("ThreadPool: %d workers ready (capacity: %d clients)", numOfThreads, numOfThreads * MAX_CLIENTS_PER_THREAD);
    return 0;
}

int ThreadPool_AddClient(ThreadPool *threadPool, int clientFd)
{
    pthread_mutex_lock(&threadPool->mutex);

    // Find worker with fewest clients
    int minClients = MAX_CLIENTS_PER_THREAD + 1;
    int target = -1;

    for (int i = 0; i < threadPool->numOfThreads; i++)
    {
        ThreadWorker *threadWorker = &threadPool->threadWorkers[i];
        pthread_mutex_lock(&threadWorker->mutex);
        if (threadWorker->clientCount < minClients)
        {
            minClients = threadWorker->clientCount;
            target = i;
        }
        pthread_mutex_unlock(&threadWorker->mutex);
    }

    if (target < 0 || minClients >= MAX_CLIENTS_PER_THREAD)
    {
        LOG_WARNING("Thread is invalid or at maximum capacity");
        pthread_mutex_unlock(&threadPool->mutex);
        return -1;
    }

    int result = ThreadWorker_AddClient(&threadPool->threadWorkers[target], clientFd);
    pthread_mutex_unlock(&threadPool->mutex);
    return result;
}

int ThreadPool_Shutdown(ThreadPool *threadPool)
{
    LOG_INFO("ThreadPool: Shutting down...");
    threadPool->isRunning = false;

    for (int i = 0; i < threadPool->numOfThreads; i++)
        ThreadWorker_Shutdown(&threadPool->threadWorkers[i]);

    pthread_mutex_destroy(&threadPool->mutex);
    free(threadPool->threadWorkers);

    LOG_INFO("Shutdown complete");
    return 0;
}