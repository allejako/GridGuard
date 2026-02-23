#include "ComputeWorker.h"
#include "ParseWorker.h"
#include "GridGuard.h"
#include "Queue.h"
#include "Compute.h"
#include "Energy.h"
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

        // Generate energy plan
        EnergyData plan;
        if (Compute_GenerateEnergyPlan(&app->compute, &parseData->forecastData, &plan) != 0)
        {
            LOG_ERROR("ComputeWorker: Failed to generate energy plan");
            const char *error = "ERROR: Failed to generate energy plan\n";
            send(parseData->clientFd, error, strlen(error), 0);
            free(item.data);
            continue;
        }

        LOG_INFO("ComputeWorker: Generated plan with %d entries, cost: %.2f SEK",
                 plan.count, plan.totalCostSek);

        // Format and send response directly to client
        char response[4096];
        int len = snprintf(response, sizeof(response),
            "\n=== Energy Plan for %s/%s ===\n"
            "Entries: %d\n"
            "Total Cost: %.2f SEK\n"
            "Grid Import: %.2f kWh\n"
            "Grid Export: %.2f kWh\n"
            "\nFirst 10 hours:\n",
            parseData->location, parseData->region,
            plan.count, plan.totalCostSek,
            plan.totalGridImportKwh, plan.totalGridExportKwh);

        int entriesToShow = plan.count < 10 ? plan.count : 10;
        for (int i = 0; i < entriesToShow; i++)
        {
            EnergyDataEntry *e = &plan.entries[i];
            len += snprintf(response + len, sizeof(response) - len,
                "[%d] Production: %.2f kWh, Price: %.2f SEK/kWh, Action: %s\n",
                i, e->productionKwh, e->spotPrice,
                EnergyAction_ToString(e->action));
        }

        len += snprintf(response + len, sizeof(response) - len, "\n");

        if (send(parseData->clientFd, response, len, 0) < 0)
        {
            LOG_ERROR("ComputeWorker: Failed to send response to client");
        }

        free(item.data);
    }

    LOG_INFO("ComputeWorker: Thread exiting");
    return NULL;
}
