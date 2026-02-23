#define _POSIX_C_SOURCE 200809L

#include "ComputeWorker.h"
#include "ParseWorker.h"
#include "GridGuard.h"
#include "Queue.h"
#include "Compute.h"
#include "Energy.h"
#include "WorkCompletion.h"
#include "Logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

// Serialize EnergyData to JSON into the WorkCompletion buffer.
// Returns 0 on success, -1 if buffer too small.
static int SerializeEnergyPlan(const EnergyData *plan,
                                const char *location,
                                const char *region,
                                char *buf,
                                size_t bufSize)
{
    int written = snprintf(buf, bufSize,
        "{\"location\":\"%s\",\"region\":\"%s\","
        "\"generated_at\":%ld,"
        "\"summary\":{\"entries\":%d,\"total_cost_sek\":%.2f,"
        "\"grid_import_kwh\":%.2f,\"grid_export_kwh\":%.2f},"
        "\"forecast\":[",
        location, region,
        (long)plan->generatedAt,
        plan->count, plan->totalCostSek,
        plan->totalGridImportKwh, plan->totalGridExportKwh);

    if (written < 0 || (size_t)written >= bufSize)
        return -1;

    int pos = written;
    int first = 1;

    for (int i = 0; i < plan->count; i++)
    {
        const EnergyDataEntry *e = &plan->entries[i];
        if (!e->valid)
            continue;

        char timeStr[32] = {0};
        struct tm tm_info;
        gmtime_r(&e->timestamp, &tm_info);
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%SZ", &tm_info);

        int n = snprintf(buf + pos, bufSize - pos,
            "%s{\"time\":\"%s\",\"signal\":\"%s\","
            "\"price_sek_kwh\":%.4f,\"solar_kwh\":%.3f,\"consumption_kwh\":%.3f}",
            first ? "" : ",",
            timeStr,
            EnergyAction_ToString(e->action),
            e->spotPrice,
            e->productionKwh,
            e->consumptionKwh);

        if (n < 0 || (size_t)(pos + n) >= bufSize)
            return -1;

        pos += n;
        first = 0;
    }

    int tail = snprintf(buf + pos, bufSize - pos, "]}");
    if (tail < 0 || (size_t)(pos + tail) >= bufSize)
        return -1;

    return 0;
}

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
        LOG_INFO("ComputeWorker: Processing %s/%s (solar=%.1fm² %.0f%%, load=%.2fkWh/h)",
                 parseData->location, parseData->region,
                 parseData->solarAreaM2,
                 parseData->solarEfficiency * 100.0,
                 parseData->consumptionKwh);

        WorkCompletion *channel = parseData->completion;

        EnergyData plan;
        if (Compute_GenerateEnergyPlan(&app->compute,
                                       &parseData->forecastData,
                                       parseData->solarAreaM2,
                                       parseData->solarEfficiency,
                                       parseData->consumptionKwh,
                                       &plan) != 0)
        {
            LOG_ERROR("ComputeWorker: Failed to generate energy plan");
            if (channel)
                WorkCompletion_SignalError(channel);
            free(item.data);
            continue;
        }

        LOG_INFO("ComputeWorker: Plan ready — %d entries, import=%.2f kWh, export=%.2f kWh",
                 plan.count, plan.totalGridImportKwh, plan.totalGridExportKwh);

        char json[WORK_COMPLETION_BUFFER_SIZE];
        if (SerializeEnergyPlan(&plan, parseData->location, parseData->region,
                                json, sizeof(json)) != 0)
        {
            LOG_ERROR("ComputeWorker: JSON serialization failed (buffer too small?)");
            if (channel)
                WorkCompletion_SignalError(channel);
            free(item.data);
            continue;
        }

        if (channel)
            WorkCompletion_Signal(channel, json);

        free(item.data);
    }

    LOG_INFO("ComputeWorker: Thread exiting");
    return NULL;
}
