#ifndef _THREADWORKER_H_
#define _THREADWORKER_H_

#include <pthread.h>
#include <stdbool.h>

typedef enum {
    CLIENT_STATE_IDLE,
    CLIENT_STATE_AUTHENTICATING,
    CLIENT_STATE_AUTHENTICATED,
    CLIENT_STATE_PROCESSING,
    CLIENT_STATE_DISCONNECTING
} ClientState;

typedef struct
{
    int id;
    int clientFds[20]; 
    int clientCount; // Current number of clients
    bool isRunning;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} ThreadWorker;

// Worker thread functions (called by ThreadPool)
int ThreadWorker_Initiate(ThreadWorker *worker);
int ThreadWorker_AddClient(ThreadWorker *worker, int client_fd);
void *ThreadWorker_Run(void *arg);
void ThreadWorker_Shutdown(ThreadWorker *worker);

#endif // _THREADWORKER_H_