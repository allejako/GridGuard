#define _POSIX_C_SOURCE 200809L

#include "compute/ComputeWorker.h"
#include "compute/Compute.h"
#include "domain/Energy.h"
#include "domain/Forecast.h"
#include "sys/WorkCompletion.h"
#include "sys/CompletionRegistry.h"
#include "ipc/ParseResult.h"
#include "sys/Logger.h"
#include "libs/cJSON.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

int ComputeWorker_Initiate(ComputeWorker *worker, const char *socketPath, const char *notifyPath, void *compute)
{
    if (!worker || !socketPath || !notifyPath || !compute)
        return -1;

    worker->socketPath = socketPath;
    worker->notifyPath = notifyPath;
    worker->compute    = compute;
    worker->isRunning  = true;
    return 0;
}

void ComputeWorker_Shutdown(ComputeWorker *worker)
{
    if (!worker)
        return;

    worker->isRunning = false;
}

static void FormatIso8601Utc(time_t t, char *buf, size_t len)
{
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

static void FormatLocalDate(time_t t, char *buf, size_t len)
{
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, len, "%Y-%m-%d", &tm);
}

// Build the JSON response that the HTTP worker forwards to the client.
// Returns a heap-allocated string (caller must free), or NULL on failure.
static char *BuildResponseJson(const EnergyData *plan, const ParseResult *req)
{
    cJSON *root = cJSON_CreateObject();
    if (!root)
        return NULL;

    cJSON_AddStringToObject(root, "user_id", req->userId);
    cJSON_AddStringToObject(root, "location", req->location);
    cJSON_AddStringToObject(root, "region", req->region);
    cJSON_AddNumberToObject(root, "generated_at", (double)plan->generatedAt);

    // --- Summary ---
    cJSON *summary = cJSON_AddObjectToObject(root, "summary");
    cJSON_AddNumberToObject(summary, "forecast_quarters", plan->count);
    cJSON_AddNumberToObject(summary, "forecast_hours", plan->count / 4.0);
    cJSON_AddNumberToObject(summary, "total_cost_sek", plan->totalCostSek);
    cJSON_AddNumberToObject(summary, "grid_import_kwh", plan->totalGridImportKwh);
    cJSON_AddNumberToObject(summary, "grid_export_kwh", plan->totalGridExportKwh);

    // Best BUY window: the single best block of cheap hours to run
    // flexible loads (EV, laundry, dishwasher). Absent if no BUY signals.
    if (plan->hasBuyWindow)
    {
        char startIso[32], endIso[32];
        FormatIso8601Utc(plan->bestBuyWindow.start, startIso, sizeof(startIso));
        FormatIso8601Utc(plan->bestBuyWindow.end, endIso, sizeof(endIso));

        cJSON *win = cJSON_AddObjectToObject(summary, "best_buy_window");
        cJSON_AddStringToObject(win, "start", startIso);
        cJSON_AddStringToObject(win, "end", endIso);
        cJSON_AddNumberToObject(win, "duration_minutes", plan->bestBuyWindow.durationMinutes);
        cJSON_AddNumberToObject(win, "avg_cost_sek_kwh", plan->bestBuyWindow.avgCostSek);
        cJSON_AddNumberToObject(win, "savings_sek", plan->bestBuyWindow.savingsSek);
    }

    // --- Raw 15-minute quarters for scheduler (all 96 entries) ---
    // LoadScheduler needs individual 15-min prices to find optimal windows.
    cJSON *quarters = cJSON_AddArrayToObject(root, "quarters");
    for (int i = 0; i < plan->count; i++)
    {
        const EnergyDataEntry *e = &plan->entries[i];
        if (!e->valid)
            continue;

        cJSON *q = cJSON_CreateObject();

        char iso[32];
        FormatIso8601Utc(e->timestamp, iso, sizeof(iso));

        cJSON_AddStringToObject(q, "time", iso);
        cJSON_AddNumberToObject(q, "spot_price_sek_kwh", e->spotPrice);
        cJSON_AddNumberToObject(q, "total_cost_sek_kwh", e->totalCostSek);
        cJSON_AddNumberToObject(q, "production_kwh", e->productionKwh);
        cJSON_AddNumberToObject(q, "consumption_kwh", e->consumptionKwh);
        cJSON_AddStringToObject(q, "action", EnergyAction_ToString(e->action));
        cJSON_AddNumberToObject(q, "price_vs_avg_pct", e->priceVsAvgPct);
        cJSON_AddNumberToObject(q, "temperature_c", e->temperature);
        cJSON_AddNumberToObject(q, "wind_speed_ms", e->windSpeed);

        cJSON_AddItemToArray(quarters, q);
    }

    // --- Forecast with intelligent filtering ---
    // Only show actionable signals (BUY/SELL/AVOID) to reduce payload size.
    // IDLE signals are noise — we skip them unless they're adjacent to an action.
    // This reduces 384 entries to ~50-100 actionable windows.
    cJSON *days = cJSON_AddArrayToObject(root, "days");

    cJSON *currentDay = NULL;
    cJSON *daySignals = NULL;
    char currentDate[16] = {0};

    // Track signal transitions to group consecutive actions into windows
    EnergyAction prevAction = ACTION_IDLE;
    time_t windowStart = 0;
    int windowQuarters = 0;
    double windowProduction = 0.0;
    double windowConsumption = 0.0;

    for (int i = 0; i <= plan->count; i++)
    {
        const EnergyDataEntry *e = (i < plan->count) ? &plan->entries[i] : NULL;
        EnergyAction currentAction = (e && e->valid) ? e->action : ACTION_IDLE;

        // Detect signal change (or end of forecast)
        bool signalChanged = (currentAction != prevAction) || (i == plan->count);

        if (signalChanged && prevAction != ACTION_IDLE && windowQuarters > 0)
        {
            // Emit the completed action window
            const EnergyDataEntry *windowEntry = &plan->entries[i - windowQuarters];

            char date[16];
            FormatLocalDate(windowEntry->timestamp, date, sizeof(date));

            if (strcmp(date, currentDate) != 0)
            {
                strncpy(currentDate, date, sizeof(currentDate) - 1);
                currentDay = cJSON_CreateObject();
                cJSON_AddStringToObject(currentDay, "date", date);
                daySignals = cJSON_AddArrayToObject(currentDay, "signals");
                cJSON_AddItemToArray(days, currentDay);
            }

            char startIso[32], endIso[32];
            FormatIso8601Utc(windowStart, startIso, sizeof(startIso));
            FormatIso8601Utc(windowEntry->timestamp + (time_t)(windowQuarters * 15 * 60), endIso, sizeof(endIso));

            cJSON *signal = cJSON_CreateObject();
            cJSON_AddStringToObject(signal, "signal", EnergyAction_ToString(prevAction));
            cJSON_AddStringToObject(signal, "start", startIso);
            cJSON_AddStringToObject(signal, "end", endIso);
            cJSON_AddNumberToObject(signal, "duration_minutes", windowQuarters * 15);
            cJSON_AddNumberToObject(signal, "price_sek_kwh", windowEntry->spotPrice);
            cJSON_AddNumberToObject(signal, "total_cost_sek_kwh", windowEntry->totalCostSek);
            cJSON_AddNumberToObject(signal, "price_vs_avg_pct", windowEntry->priceVsAvgPct);
            cJSON_AddNumberToObject(signal, "solar_kwh", windowProduction);
            cJSON_AddNumberToObject(signal, "consumption_kwh", windowConsumption);
            cJSON_AddItemToArray(daySignals, signal);

            // Reset window
            windowQuarters = 0;
            windowProduction = 0.0;
            windowConsumption = 0.0;
        }

        if (e && e->valid && currentAction != ACTION_IDLE)
        {
            if (currentAction != prevAction)
            {
                // Start new window
                windowStart = e->timestamp;
            }
            windowQuarters++;
            windowProduction += e->productionKwh;
            windowConsumption += e->consumptionKwh;
        }

        prevAction = currentAction;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

static int ConnectToParser(const char *socketPath)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(fd);
        return -1;
    }

    return fd;
}

void *ComputeWorker_Run(void *arg)
{
    ComputeWorker *worker = (ComputeWorker *)arg;
    if (!worker)
        return NULL;

    Compute *compute = (Compute *)worker->compute;
    LOG_INFO("ComputeWorker: started");

    // Wait for Parser to create notify FIFO, then open it (blocking until Parser opens write end)
    LOG_INFO("ComputeWorker: Waiting for Parser to create and open notify FIFO %s", worker->notifyPath);

    int notifyFd = -1;
    for (int attempt = 0; attempt < 60 && worker->isRunning; attempt++)
    {
        // Check if FIFO exists first
        struct stat st;
        if (stat(worker->notifyPath, &st) == 0)
        {
            // FIFO exists, now open it (this will block until Parser opens write end)
            notifyFd = open(worker->notifyPath, O_RDONLY);
            if (notifyFd >= 0)
            {
                break;
            }

            LOG_ERROR("ComputeWorker: Failed to open notify FIFO %s: %s", worker->notifyPath, strerror(errno));
            free(worker);
            return NULL;
        }

        // FIFO doesn't exist yet, wait for Parser to create it
        sleep(1);
    }

    if (notifyFd < 0)
    {
        LOG_ERROR("ComputeWorker: Notify FIFO %s not available after 60s", worker->notifyPath);
        free(worker);
        return NULL;
    }

    LOG_INFO("ComputeWorker: Connected to Parser via notify FIFO, listening for notifications");

    while (worker->isRunning)
    {
        // Wait for notification from Parser that data is ready
        char notifySignal;
        ssize_t n = read(notifyFd, &notifySignal, 1);
        if (n <= 0)
        {
            if (n == 0)
            {
                LOG_INFO("ComputeWorker: Parser closed notify FIFO, exiting");
                break;
            }
            LOG_ERROR("ComputeWorker: Failed to read from notify FIFO: %s", strerror(errno));
            sleep(1);
            continue;
        }

        LOG_INFO("ComputeWorker: Received notification, connecting to Parser");

        // Now connect to Parser to receive the data
        int fd = ConnectToParser(worker->socketPath);
        if (fd < 0)
        {
            LOG_ERROR("ComputeWorker: Failed to connect to Parser after notification");
            continue;
        }

        ParseResult result;
        size_t totalSize = sizeof(result);
        size_t totalRead = 0;
        char *buf = (char *)&result;

        // Read in loop to handle partial reads
        while (totalRead < totalSize)
        {
            ssize_t n = read(fd, buf + totalRead, totalSize - totalRead);
            if (n < 0)
            {
                LOG_ERROR("ComputeWorker: read failed: %s (read %zu/%zu bytes)", strerror(errno), totalRead, totalSize);
                break;
            }
            if (n == 0)
            {
                if (totalRead == 0)
                {
                    // Parser exited cleanly before sending any data
                    close(fd);
                    goto exit_loop;
                }
                LOG_WARNING("ComputeWorker: Parser closed socket after %zu/%zu bytes", totalRead, totalSize);
                break;
            }
            totalRead += n;
        }

        close(fd);

        if (totalRead != totalSize)
        {
            LOG_ERROR("ComputeWorker: incomplete read (%zu/%zu bytes)", totalRead, totalSize);
            continue;
        }

        LOG_INFO("ComputeWorker: %s — solar %.1fm² %.0f%%, avg load %.2f kWh/h, region %s", result.userId, result.solarAreaM2, result.solarEfficiency * 100.0, result.consumptionKwh, result.region);

        WorkCompletion *completion = CompletionRegistry_Find(result.userId);
        if (!completion)
        {
            LOG_ERROR("ComputeWorker: no pending request for user %s", result.userId);
            continue;
        }

        EnergyData plan;
        int rc = Compute_GenerateEnergyPlan(compute, &result.forecastData, result.solarAreaM2, result.solarEfficiency, result.consumptionKwh, result.gridFeeLow, result.gridFeeNormal, result.gridFeeHigh, &plan);

        if (rc != 0)
        {
            LOG_ERROR("ComputeWorker: plan generation failed for %s", result.userId);
            CompletionRegistry_Unregister(result.userId);
            WorkCompletion_SignalError(completion);
            continue;
        }

        char *json = BuildResponseJson(&plan, &result);
        if (!json)
        {
            LOG_ERROR("ComputeWorker: JSON allocation failed for %s", result.userId);
            CompletionRegistry_Unregister(result.userId);
            WorkCompletion_SignalError(completion);
            continue;
        }

        if (strlen(json) >= WORK_COMPLETION_BUFFER_SIZE)
        {
            LOG_ERROR("ComputeWorker: JSON response too large for %s (%zu bytes)", result.userId, strlen(json));
            free(json);
            CompletionRegistry_Unregister(result.userId);
            WorkCompletion_SignalError(completion);
            continue;
        }

        // Unregister before signalling so a subsequent request from the same
        // user can register immediately without racing against a stale slot.
        CompletionRegistry_Unregister(result.userId);
        WorkCompletion_Signal(completion, json);
        free(json);
    }

exit_loop:
    close(notifyFd);
    LOG_INFO("ComputeWorker: exiting");

    // Free the worker struct that was allocated in GridGuard_Initiate
    free(worker);
    return NULL;
}
