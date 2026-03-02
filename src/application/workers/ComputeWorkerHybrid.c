#define _POSIX_C_SOURCE 200809L

#include "ComputeWorkerHybrid.h"
#include "Compute.h"
#include "Energy.h"
#include "Forecast.h"
#include "WorkCompletion.h"
#include "CompletionRegistry.h"
#include "Logger.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <stdio.h>

typedef struct
{
    char userId[64];
    char location[64];
    char region[16];
    double solarAreaM2;
    double solarEfficiency;
    double consumptionKwh;
    double gridFee_low;
    double gridFee_normal;
    double gridFee_high;
    ForecastData forecastData;
} ParseResult;

// Samma serialisering som ComputeWorker.c
static int serialize_energy_plan(const EnergyData *plan, const char *userId,
                                  const char *location, const char *region,
                                  char *buf, size_t bufSize)
{
    int written = snprintf(buf, bufSize,
        "{\"user_id\":\"%s\",\"location\":\"%s\",\"region\":\"%s\","
        "\"generated_at\":%ld,"
        "\"summary\":{\"entries\":%d,\"total_cost_sek\":%.2f,"
        "\"grid_import_kwh\":%.2f,\"grid_export_kwh\":%.2f},"
        "\"forecast\":[",
        userId, location, region,
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

void *ComputeWorkerHybrid_Run(void *arg)
{
    ComputeWorkerHybrid *worker = (ComputeWorkerHybrid *)arg;
    if (!worker)
        return NULL;

    Compute *compute = (Compute *)worker->compute;
    LOG_INFO("ComputeWorkerHybrid: Thread started (Unix socket client)");

    while (worker->isRunning)
    {
        // Connect till Parse-process via Unix socket (Vecka 5: socket(), connect())
        int clientSocket = socket(AF_UNIX, SOCK_STREAM, 0);
        if (clientSocket < 0)
        {
            LOG_ERROR("ComputeWorkerHybrid: Failed to create socket");
            sleep(1);
            continue;
        }

        struct sockaddr_un addr = {0};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, worker->socketPath, sizeof(addr.sun_path) - 1);

        if (connect(clientSocket, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        {
            // Parse-processen kanske inte har startat än, eller är upptagen
            close(clientSocket);
            sleep(1);
            continue;
        }

        LOG_DEBUG("ComputeWorkerHybrid: Connected to Parse process");

        // Läs ParseResult från Unix socket (Vecka 5: read() på connected socket)
        ParseResult parseResult;
        ssize_t bytesRead = read(clientSocket, &parseResult, sizeof(parseResult));

        if (bytesRead == 0)
        {
            LOG_INFO("ComputeWorkerHybrid: Socket closed by Parse process");
            close(clientSocket);
            break;
        }

        if (bytesRead != sizeof(parseResult))
        {
            LOG_ERROR("ComputeWorkerHybrid: Partial read from socket (%zd bytes)", bytesRead);
            close(clientSocket);
            continue;
        }

        close(clientSocket);

        LOG_INFO("ComputeWorkerHybrid: Processing %s/%s (solar=%.1fm² %.0f%%, load=%.2fkWh/h)",
                 parseResult.userId, parseResult.region,
                 parseResult.solarAreaM2,
                 parseResult.solarEfficiency * 100.0,
                 parseResult.consumptionKwh);

        // Hitta WorkCompletion via CompletionRegistry (Vecka 3: mutex-skyddad global map)
        WorkCompletion *completion = FindCompletionByUserId(parseResult.userId);
        if (!completion)
        {
            LOG_ERROR("ComputeWorkerHybrid: No completion channel for userId %s", parseResult.userId);
            continue;
        }

        // Generera energy plan
        EnergyData plan;
        if (Compute_GenerateEnergyPlan(compute,
                                       &parseResult.forecastData,
                                       parseResult.solarAreaM2,
                                       parseResult.solarEfficiency,
                                       parseResult.consumptionKwh,
                                       parseResult.gridFee_low,
                                       parseResult.gridFee_normal,
                                       parseResult.gridFee_high,
                                       &plan) != 0)
        {
            LOG_ERROR("ComputeWorkerHybrid: Failed to generate energy plan");
            WorkCompletion_SignalError(completion);
            continue;
        }

        LOG_INFO("ComputeWorkerHybrid: Plan ready — %d entries, import=%.2f kWh, export=%.2f kWh",
                 plan.count, plan.totalGridImportKwh, plan.totalGridExportKwh);

        // Serialisera till JSON
        char json[WORK_COMPLETION_BUFFER_SIZE];
        if (serialize_energy_plan(&plan, parseResult.userId, parseResult.location,
                                   parseResult.region, json, sizeof(json)) != 0)
        {
            LOG_ERROR("ComputeWorkerHybrid: JSON serialization failed");
            WorkCompletion_SignalError(completion);
            continue;
        }

        // Signalera HTTP-tråden (Vecka 3: pthread_cond_signal via WorkCompletion)
        WorkCompletion_Signal(completion, json);
    }

    LOG_INFO("ComputeWorkerHybrid: Thread exiting");
    return NULL;
}
