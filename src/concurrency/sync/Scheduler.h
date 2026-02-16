#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

#include <pthread.h>

// Scheduling policies for work distribution
typedef enum
{
    SCHEDULING_POLICY_LEAST_CONNECTIONS,  // Worker with fewest clients (default)
    SCHEDULING_POLICY_ROUND_ROBIN,        // Rotate through workers 1->2->3->1
    SCHEDULING_POLICY_RANDOM              // Random worker selection
} SchedulingPolicy;

// Worker load info - passed to scheduler when selecting next worker
typedef struct
{
    int workerId;
    int currentLoad;  // Number of active clients/tasks
    int capacity;     // Max capacity for this worker
} WorkerLoad;

// Scheduler - handles work distribution logic
typedef struct
{
    SchedulingPolicy policy;
    int lastSelectedWorker;  // For round-robin
    pthread_mutex_t mutex;
} Scheduler;

int Scheduler_Initiate(Scheduler *scheduler, SchedulingPolicy policy);
int Scheduler_SelectWorker(Scheduler *scheduler, const WorkerLoad *workers, int numWorkers);
const char *Scheduler_GetPolicyName(SchedulingPolicy policy);
void Scheduler_Shutdown(Scheduler *scheduler);

#endif // _SCHEDULER_H_
