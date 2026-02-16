#include "CacheStage.h"
#include "PipelineOrchestrator.h"
#include "Queue.h"
#include "Cache.h"
#include "Logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>

void *CacheStage_Work(void *arg)
{
    Pipeline *pipeline = (Pipeline *)arg;
    LOG_INFO("Cache thread started");

    while (pipeline->isRunning)
    {
        QueueItem item;
        if (Queue_Pop(&pipeline->computeQueue, &item) != 0)
            break;

        if (item.type != DATA_TYPE_ENERGY_PLAN)
        {
            free(item.data);
            continue;
        }

        ComputeResult *result = (ComputeResult *)item.data;
        LOG_INFO("Cache: Processing result for %s/%s", result->location, result->region);

        // Store in cache
        Cache_Store(&pipeline->cache, result->location, result->region, &result->plan);

        // Format response for client
        EnergyData *plan = &result->plan;
        char response[4096];
        int len = snprintf(response, sizeof(response),
            "\n=== Energy Plan for %s/%s ===\n"
            "Entries: %d\n"
            "Total Cost: %.2f SEK\n"
            "Grid Import: %.2f kWh\n"
            "Grid Export: %.2f kWh\n"
            "\nFirst 10 hours:\n",
            result->location, result->region,
            plan->count, plan->totalCostSek,
            plan->totalGridImportKwh, plan->totalGridExportKwh);

        int entriesToShow = plan->count < 10 ? plan->count : 10;
        for (int i = 0; i < entriesToShow; i++)
        {
            EnergyDataEntry *e = &plan->entries[i];
            len += snprintf(response + len, sizeof(response) - len,
                "[%d] Production: %.2f kWh, Price: %.2f SEK/kWh, Action: %s\n",
                i, e->productionKwh, e->spotPrice,
                EnergyAction_ToString(e->action));
        }

        len += snprintf(response + len, sizeof(response) - len, "\n");

        if (send(result->clientFd, response, len, 0) < 0)
        {
            LOG_ERROR("Cache: Failed to send response to client");
        }

        free(item.data);
    }

    LOG_INFO("Cache thread exiting");
    return NULL;
}
