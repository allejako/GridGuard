#include "PipelineOrchestrator.h"
#include "FetchStage.h"
#include "ParseStage.h"
#include "ComputeStage.h"
#include "Queue.h"
#include "Logger.h"

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

    // Initialize Compute
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

    // Start worker threads (using stage functions)
    if (pthread_create(&pipeline->fetchThread, NULL, FetchStage_Work, pipeline) != 0)
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

    if (pthread_create(&pipeline->parseThread, NULL, ParseStage_Work, pipeline) != 0)
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

    if (pthread_create(&pipeline->computeThread, NULL, ComputeStage_Work, pipeline) != 0)
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
