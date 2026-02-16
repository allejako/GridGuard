#include "Scheduler.h"
#include "Logger.h"
#include <stdlib.h>

int Scheduler_Initiate(Scheduler *scheduler, SchedulingPolicy policy)
{
    if (!scheduler)
        return -1;

    scheduler->policy = policy;
    scheduler->lastSelectedWorker = -1;
    pthread_mutex_init(&scheduler->mutex, NULL);

    LOG_INFO("Scheduler: Initiated with policy '%s'", Scheduler_GetPolicyName(policy));
    return 0;
}

int Scheduler_SelectWorker(Scheduler *scheduler, const WorkerLoad *workers, int numWorkers)
{
    if (!scheduler || !workers || numWorkers <= 0)
        return -1;

    pthread_mutex_lock(&scheduler->mutex);

    int selected = -1;

    switch (scheduler->policy)
    {
    case SCHEDULING_POLICY_LEAST_CONNECTIONS:
    {
        // Find worker with lowest load that has capacity
        int minLoad = -1;
        for (int i = 0; i < numWorkers; i++)
        {
            if (workers[i].currentLoad >= workers[i].capacity)
                continue; // Worker at capacity

            if (minLoad == -1 || workers[i].currentLoad < minLoad)
            {
                minLoad = workers[i].currentLoad;
                selected = i;
            }
        }
        break;
    }

    case SCHEDULING_POLICY_ROUND_ROBIN:
    {
        // Try each worker starting from last + 1
        int attempts = 0;
        int next = (scheduler->lastSelectedWorker + 1) % numWorkers;

        while (attempts < numWorkers)
        {
            if (workers[next].currentLoad < workers[next].capacity)
            {
                selected = next;
                scheduler->lastSelectedWorker = next;
                break;
            }
            next = (next + 1) % numWorkers;
            attempts++;
        }
        break;
    }

    case SCHEDULING_POLICY_RANDOM:
    {
        // Find available workers and pick random
        int available[numWorkers];
        int availableCount = 0;

        for (int i = 0; i < numWorkers; i++)
        {
            if (workers[i].currentLoad < workers[i].capacity)
                available[availableCount++] = i;
        }

        if (availableCount > 0)
        {
            int randomIdx = rand() % availableCount;
            selected = available[randomIdx];
        }
        break;
    }

    default:
        LOG_ERROR("Scheduler: Unknown policy %d", scheduler->policy);
        break;
    }

    pthread_mutex_unlock(&scheduler->mutex);
    return selected;
}

const char *Scheduler_GetPolicyName(SchedulingPolicy policy)
{
    switch (policy)
    {
    case SCHEDULING_POLICY_LEAST_CONNECTIONS:
        return "Least Connections";
    case SCHEDULING_POLICY_ROUND_ROBIN:
        return "Round Robin";
    case SCHEDULING_POLICY_RANDOM:
        return "Random";
    default:
        return "Unknown";
    }
}

void Scheduler_Shutdown(Scheduler *scheduler)
{
    if (!scheduler)
        return;

    pthread_mutex_destroy(&scheduler->mutex);
    LOG_INFO("Scheduler: Shutdown complete");
}
