#ifndef _THREADWORKER_H_
#define _THREADWORKER_H_

#include <pthread.h>
#include <stdbool.h>

#include "config.h"

typedef enum {
    CLIENT_DISCONNECTED = 0,
    CLIENT_CONNECTED,
    CLIENT_AUTHENTICATING,
    CLIENT_READY,
    CLIENT_PROCESSING
} ClientState;

typedef struct {
    int fd;
    ClientState state;
    char buffer[CLIENT_BUFFER_SIZE];
    int bufferLen;
} Client;

typedef struct {
    int id;
    pthread_t thread;
    Client clients[MAX_CLIENTS_PER_THREAD];
    int clientCount;
    bool isRunning;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} ThreadWorker;

int ThreadWorker_Initiate(ThreadWorker *worker, int id);
int ThreadWorker_AddClient(ThreadWorker *worker, int clientFd);
void ThreadWorker_Shutdown(ThreadWorker *worker);

#endif // _THREADWORKER_H_