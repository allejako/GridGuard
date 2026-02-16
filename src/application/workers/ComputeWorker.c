#include "ComputeWorker.h"
#include "ParseWorker.h"
#include "CacheWorker.h"
#include "GridGuard.h"
#include "Queue.h"
#include "Compute.h"
#include "Cache.h"
#include "EnergyData.h"
#include "Logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>

void *ComputeWorker_Run(void *arg)
{
    GridGuard *app = (GridGuard *)arg;
    LOG_INFO("ComputeWorker: Thread started");

    while (app->isRunning)
    {
        QueueItem item;
        if (Queue_Pop(&app->parseQueue, &item) != 0)
            break;

        if (item.type != DATA_TYPE_PARSED_DATA)
        {
            free(item.data);
            continue;
        }

        ParseResult *parseData = (ParseResult *)item.data;
        LOG_INFO("ComputeWorker: Processing data for %s/%s", parseData->location, parseData->region);

        // Check cache first - if we have a valid cached plan, skip computation
        EnergyData cachedPlan;
        if (Cache_Lookup(&app->cache, parseData->location, parseData->region, &cachedPlan) == 0)
        {
            LOG_INFO("ComputeWorker: Cache HIT for %s/%s, skipping computation", parseData->location, parseData->region);

            // Push cached result to cache worker for sending to client
            ComputeResult *result = malloc(sizeof(ComputeResult));
            if (result)
            {
                result->plan = cachedPlan;
                strncpy(result->location, parseData->location, sizeof(result->location) - 1);
                result->location[sizeof(result->location) - 1] = '\0';
                strncpy(result->region, parseData->region, sizeof(result->region) - 1);
                result->region[sizeof(result->region) - 1] = '\0';
                result->clientFd = parseData->clientFd;

                Queue_Push(&app->computeQueue, result, sizeof(ComputeResult), DATA_TYPE_ENERGY_PLAN);
                free(result);
            }

            free(item.data);
            continue;
        }

        // Cache MISS - generate energy plan
        EnergyData plan;
        if (Compute_GenerateEnergyPlan(&app->compute, &parseData->weather, &parseData->prices, &plan) == 0)
        {
            LOG_INFO("ComputeWorker: Generated plan with %d entries, cost: %.2f SEK",
                     plan.count, plan.totalCostSek);

            // Push result to cache worker
            ComputeResult *result = malloc(sizeof(ComputeResult));
            if (result)
            {
                result->plan = plan;
                strncpy(result->location, parseData->location, sizeof(result->location) - 1);
                result->location[sizeof(result->location) - 1] = '\0';
                strncpy(result->region, parseData->region, sizeof(result->region) - 1);
                result->region[sizeof(result->region) - 1] = '\0';
                result->clientFd = parseData->clientFd;

                Queue_Push(&app->computeQueue, result, sizeof(ComputeResult), DATA_TYPE_ENERGY_PLAN);
                free(result);
            }
        }
        else
        {
            LOG_ERROR("ComputeWorker: Failed to generate energy plan");
            const char *error = "ERROR: Failed to generate energy plan\n";
            send(parseData->clientFd, error, strlen(error), 0);
        }

        free(item.data);
    }

    LOG_INFO("ComputeWorker: Thread exiting");
    return NULL;
}
