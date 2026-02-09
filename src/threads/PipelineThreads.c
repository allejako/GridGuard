#include "PipelineThreads.h"
#include "APIEndpoints.h"
#include "ForecastData.h"
#include "Config.h"
#include "Logger.h"
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

// ========== QUEUE IMPLEMENTATION (Internal) ==========

static int Queue_Initiate(Queue *queue)
{
    if (!queue)
        return -1;

    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->isShutdown = false;

    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->notEmpty, NULL);
    pthread_cond_init(&queue->notFull, NULL);

    LOG_INFO("Queue initiated");
    return 0;
}

static int Queue_Push(Queue *queue, void *data, size_t size, int type)
{
    if (!queue || !data)
        return -1;

    pthread_mutex_lock(&queue->mutex);

    // Wait if queue is full
    while (queue->count >= QUEUE_SIZE && !queue->isShutdown)
        pthread_cond_wait(&queue->notFull, &queue->mutex);

    if (queue->isShutdown)
    {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    // Allocate and copy data
    queue->items[queue->tail].data = malloc(size);
    if (!queue->items[queue->tail].data)
    {
        pthread_mutex_unlock(&queue->mutex);
        LOG_ERROR("Queue: Failed to allocate memory for item");
        return -1;
    }

    memcpy(queue->items[queue->tail].data, data, size);
    queue->items[queue->tail].size = size;
    queue->items[queue->tail].type = type;

    queue->tail = (queue->tail + 1) % QUEUE_SIZE;
    queue->count++;

    // Signal that queue is not empty
    pthread_cond_signal(&queue->notEmpty);
    pthread_mutex_unlock(&queue->mutex);

    return 0;
}

static int Queue_Pop(Queue *queue, QueueItem *item)
{
    if (!queue || !item)
        return -1;

    pthread_mutex_lock(&queue->mutex);

    // Wait if queue is empty
    while (queue->count == 0 && !queue->isShutdown)
        pthread_cond_wait(&queue->notEmpty, &queue->mutex);

    if (queue->isShutdown && queue->count == 0)
    {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    // Copy item
    *item = queue->items[queue->head];

    queue->head = (queue->head + 1) % QUEUE_SIZE;
    queue->count--;

    // Signal that queue is not full
    pthread_cond_signal(&queue->notFull);
    pthread_mutex_unlock(&queue->mutex);

    return 0;
}

static void Queue_Shutdown(Queue *queue)
{
    if (!queue)
        return;

    pthread_mutex_lock(&queue->mutex);
    queue->isShutdown = true;

    // Free all remaining items
    for (int i = 0; i < queue->count; i++)
    {
        int idx = (queue->head + i) % QUEUE_SIZE;
        if (queue->items[idx].data)
        {
            free(queue->items[idx].data);
            queue->items[idx].data = NULL;
        }
    }

    // Wake up all waiting threads
    pthread_cond_broadcast(&queue->notEmpty);
    pthread_cond_broadcast(&queue->notFull);
    pthread_mutex_unlock(&queue->mutex);

    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->notEmpty);
    pthread_cond_destroy(&queue->notFull);

    LOG_INFO("Queue shutdown");
}

// ========== INTERNAL DATA STRUCTURES ==========

typedef struct
{
    char weatherJson[8192];
    char priceJson[16384];  
    char location[64];
    char region[16];
    int clientFd;
} FetchResult;

typedef struct
{
    OpenMeteoResponse weather;
    ElprisetResponse prices;
    char location[64];
    char region[16];
    int clientFd;
} ParseResult;

// FETCH THREAD 

static void *FetchThread_Work(void *arg)
{
    Pipeline *pipeline = (Pipeline *)arg;
    LOG_INFO("Fetch thread started");

    while (pipeline->isRunning)
    {
        QueueItem item;
        if (Queue_Pop(&pipeline->requestQueue, &item) != 0)
            break;

        if (item.type != DATA_TYPE_REQUEST)
        {
            free(item.data);
            continue;
        }

        PipelineRequest *request = (PipelineRequest *)item.data;
        LOG_INFO("Fetch: Processing request for %s/%s", request->location, request->region);

        // Allocate result
        FetchResult *result = calloc(1, sizeof(FetchResult));
        if (!result)
        {
            LOG_ERROR("Fetch: Failed to allocate result");
            free(item.data);
            continue;
        }

        strncpy(result->location, request->location, sizeof(result->location) - 1);
        strncpy(result->region, request->region, sizeof(result->region) - 1);
        result->clientFd = request->clientFd;

        // Build URLs
        char weatherUrl[512];
        char priceUrl[256];
        BuildWeatherApiUrl(weatherUrl, sizeof(weatherUrl), WEATHER_LAT, WEATHER_LON);
        BuildSpotPriceApiUrl(priceUrl, sizeof(priceUrl), request->region, NULL);

        // Fetch weather data
        FetchResponse weatherResp;
        if (Fetcher_Fetch(&pipeline->fetcher, weatherUrl, &weatherResp) == 0)
        {
            strncpy(result->weatherJson, weatherResp.data, sizeof(result->weatherJson) - 1);
            Fetcher_FreeResponse(&weatherResp);
            LOG_INFO("Fetch: Got weather data (%zu bytes)", strlen(result->weatherJson));
        }
        else
        {
            LOG_ERROR("Fetch: Failed to get weather data");
        }

        // Fetch price data
        FetchResponse priceResp;
        if (Fetcher_Fetch(&pipeline->fetcher, priceUrl, &priceResp) == 0)
        {
            strncpy(result->priceJson, priceResp.data, sizeof(result->priceJson) - 1);
            Fetcher_FreeResponse(&priceResp);
            LOG_INFO("Fetch: Got price data (%zu bytes)", strlen(result->priceJson));
        }
        else
        {
            LOG_ERROR("Fetch: Failed to get price data");
        }

        // Push to parse queue
        if (Queue_Push(&pipeline->fetchQueue, result, sizeof(FetchResult), DATA_TYPE_API_RESPONSE) != 0)
        {
            LOG_ERROR("Fetch: Failed to push to parse queue");
            free(result);
        }
        else
        {
            free(result);
        }

        free(item.data);
    }

    LOG_INFO("Fetch thread exiting");
    return NULL;
}

// PARSE THREAD 

static void *ParseThread_Work(void *arg)
{
    Pipeline *pipeline = (Pipeline *)arg;
    LOG_INFO("Parse thread started");

    while (pipeline->isRunning)
    {
        QueueItem item;
        if (Queue_Pop(&pipeline->fetchQueue, &item) != 0)
            break;

        if (item.type != DATA_TYPE_API_RESPONSE)
        {
            free(item.data);
            continue;
        }

        FetchResult *fetchData = (FetchResult *)item.data;
        LOG_INFO("Parse: Processing data for %s/%s", fetchData->location, fetchData->region);

        // Allocate result
        ParseResult *result = calloc(1, sizeof(ParseResult));
        if (!result)
        {
            LOG_ERROR("Parse: Failed to allocate result");
            free(item.data);
            continue;
        }

        strncpy(result->location, fetchData->location, sizeof(result->location) - 1);
        strncpy(result->region, fetchData->region, sizeof(result->region) - 1);
        result->clientFd = fetchData->clientFd;

        // Parse weather
        if (strlen(fetchData->weatherJson) > 0)
        {
            if (Parser_ParseOpenMeteo(&pipeline->parser, fetchData->weatherJson, &result->weather) == 0)
            {
                LOG_INFO("Parse: Parsed %d weather entries", result->weather.count);
            }
            else
            {
                LOG_ERROR("Parse: Failed to parse weather data");
            }
        }

        // Parse prices
        if (strlen(fetchData->priceJson) > 0)
        {
            if (Parser_ParseElpriset(&pipeline->parser, fetchData->priceJson, &result->prices) == 0)
            {
                LOG_INFO("Parse: Parsed %d price entries", result->prices.count);
            }
            else
            {
                LOG_ERROR("Parse: Failed to parse price data");
            }
        }

        // Push to compute queue
        if (Queue_Push(&pipeline->parseQueue, result, sizeof(ParseResult), DATA_TYPE_PARSED_DATA) != 0)
        {
            LOG_ERROR("Parse: Failed to push to compute queue");
            free(result);
        }
        else
        {
            free(result);
        }

        free(item.data);
    }

    LOG_INFO("Parse thread exiting");
    return NULL;
}

// COMPUTE THREAD 

static void *ComputeThread_Work(void *arg)
{
    Pipeline *pipeline = (Pipeline *)arg;
    LOG_INFO("Compute thread started");

    while (pipeline->isRunning)
    {
        QueueItem item;
        if (Queue_Pop(&pipeline->parseQueue, &item) != 0)
            break;

        if (item.type != DATA_TYPE_PARSED_DATA)
        {
            free(item.data);
            continue;
        }

        ParseResult *parseData = (ParseResult *)item.data;
        LOG_INFO("Compute: Processing data for %s/%s", parseData->location, parseData->region);

        // Generate energy plan from weather and price data
        EnergyData plan;
        if (Compute_GenerateEnergyPlan(&pipeline->compute, &parseData->weather, &parseData->prices, &plan) == 0)
        {
            LOG_INFO("Compute: Generated plan with %d entries, cost: %.2f SEK",
                     plan.count, plan.totalCostSek);

            // Format response for client
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

            // Add first 10 entries
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

            // Send to client
            if (send(parseData->clientFd, response, len, 0) < 0)
            {
                LOG_ERROR("Compute: Failed to send response to client");
            }
        }
        else
        {
            LOG_ERROR("Compute: Failed to generate energy plan");
            const char *error = "ERROR: Failed to generate energy plan\n";
            send(parseData->clientFd, error, strlen(error), 0);
        }

        free(item.data);
    }

    LOG_INFO("Compute thread exiting");
    return NULL;
}

// ========== PIPELINE OPERATIONS ==========

int Pipeline_Initiate(Pipeline *pipeline)
{
    if (!pipeline)
        return -1;

    LOG_INFO("Pipeline: Initiating...");

    // Initialize queues
    if (Queue_Initiate(&pipeline->requestQueue) != 0)
    {
        LOG_ERROR("Pipeline: Failed to initiate request queue");
        return -1;
    }
    if (Queue_Initiate(&pipeline->fetchQueue) != 0)
    {
        LOG_ERROR("Pipeline: Failed to initiate fetch queue");
        Queue_Shutdown(&pipeline->requestQueue);
        return -1;
    }
    if (Queue_Initiate(&pipeline->parseQueue) != 0)
    {
        LOG_ERROR("Pipeline: Failed to initiate parse queue");
        Queue_Shutdown(&pipeline->requestQueue);
        Queue_Shutdown(&pipeline->fetchQueue);
        return -1;
    }

    // Initialize components
    if (Fetcher_Initiate(&pipeline->fetcher) != 0)
    {
        LOG_ERROR("Pipeline: Failed to initiate APIFetcher");
        Queue_Shutdown(&pipeline->requestQueue);
        Queue_Shutdown(&pipeline->fetchQueue);
        Queue_Shutdown(&pipeline->parseQueue);
        return -1;
    }

    if (Parser_Initiate(&pipeline->parser) != 0)
    {
        LOG_ERROR("Pipeline: Failed to initiate Parser");
        Fetcher_Shutdown(&pipeline->fetcher);
        Queue_Shutdown(&pipeline->requestQueue);
        Queue_Shutdown(&pipeline->fetchQueue);
        Queue_Shutdown(&pipeline->parseQueue);
        return -1;
    }

    // Configure compute with default settings
    SolarConfig solar = {
        .panelEfficiency = 0.18,
        .panelAreaM2 = 20.0,
        .orientationDegrees = 180.0,
        .tiltDegrees = 35.0,
        .peakPowerKw = 3.6
    };

    BatteryConfig battery = {
        .capacityKwh = 10.0,
        .maxChargeRateKw = 5.0,
        .maxDischargeRateKw = 5.0,
        .minSocPercent = 20.0,
        .maxSocPercent = 95.0,
        .currentSocPercent = 50.0,
        .efficiency = 0.9
    };

    ConsumptionProfile consumption = {
        .baseLoadKw = 0.5,
        .peakLoadKw = 3.0,
        .averageDailyKwh = 15.0
    };

    if (Compute_Initiate(&pipeline->compute, &solar, &battery, &consumption) != 0)
    {
        LOG_ERROR("Pipeline: Failed to initiate Compute");
        Parser_Shutdown(&pipeline->parser);
        Fetcher_Shutdown(&pipeline->fetcher);
        Queue_Shutdown(&pipeline->requestQueue);
        Queue_Shutdown(&pipeline->fetchQueue);
        Queue_Shutdown(&pipeline->parseQueue);
        return -1;
    }

    pipeline->isRunning = true;
    pthread_mutex_init(&pipeline->mutex, NULL);

    // Start worker threads
    if (pthread_create(&pipeline->fetchThread, NULL, FetchThread_Work, pipeline) != 0)
    {
        LOG_ERROR("Pipeline: Failed to create fetch thread");
        Compute_Shutdown(&pipeline->compute);
        Parser_Shutdown(&pipeline->parser);
        Fetcher_Shutdown(&pipeline->fetcher);
        Queue_Shutdown(&pipeline->requestQueue);
        Queue_Shutdown(&pipeline->fetchQueue);
        Queue_Shutdown(&pipeline->parseQueue);
        return -1;
    }

    if (pthread_create(&pipeline->parseThread, NULL, ParseThread_Work, pipeline) != 0)
    {
        LOG_ERROR("Pipeline: Failed to create parse thread");
        pipeline->isRunning = false;
        pthread_join(pipeline->fetchThread, NULL);
        Compute_Shutdown(&pipeline->compute);
        Parser_Shutdown(&pipeline->parser);
        Fetcher_Shutdown(&pipeline->fetcher);
        Queue_Shutdown(&pipeline->requestQueue);
        Queue_Shutdown(&pipeline->fetchQueue);
        Queue_Shutdown(&pipeline->parseQueue);
        return -1;
    }

    if (pthread_create(&pipeline->computeThread, NULL, ComputeThread_Work, pipeline) != 0)
    {
        LOG_ERROR("Pipeline: Failed to create compute thread");
        pipeline->isRunning = false;
        pthread_join(pipeline->fetchThread, NULL);
        pthread_join(pipeline->parseThread, NULL);
        Compute_Shutdown(&pipeline->compute);
        Parser_Shutdown(&pipeline->parser);
        Fetcher_Shutdown(&pipeline->fetcher);
        Queue_Shutdown(&pipeline->requestQueue);
        Queue_Shutdown(&pipeline->fetchQueue);
        Queue_Shutdown(&pipeline->parseQueue);
        return -1;
    }

    LOG_INFO("Pipeline: Initiated with 3 worker threads");
    return 0;
}

int Pipeline_SubmitRequest(Pipeline *pipeline, const PipelineRequest *request)
{
    if (!pipeline || !request)
        return -1;

    LOG_INFO("Pipeline: Submitting request for %s/%s from client FD %d",
             request->location, request->region, request->clientFd);

    return Queue_Push(&pipeline->requestQueue, (void *)request,
                      sizeof(PipelineRequest), DATA_TYPE_REQUEST);
}

void Pipeline_Shutdown(Pipeline *pipeline)
{
    if (!pipeline)
        return;

    LOG_INFO("Pipeline: Shutting down...");

    pipeline->isRunning = false;

    // Shutdown queues to wake up waiting threads
    Queue_Shutdown(&pipeline->requestQueue);
    Queue_Shutdown(&pipeline->fetchQueue);
    Queue_Shutdown(&pipeline->parseQueue);

    // Wait for all threads to finish
    pthread_join(pipeline->fetchThread, NULL);
    pthread_join(pipeline->parseThread, NULL);
    pthread_join(pipeline->computeThread, NULL);

    // Cleanup components
    Compute_Shutdown(&pipeline->compute);
    Parser_Shutdown(&pipeline->parser);
    Fetcher_Shutdown(&pipeline->fetcher);

    pthread_mutex_destroy(&pipeline->mutex);

    LOG_INFO("Pipeline: Shutdown complete");
}
