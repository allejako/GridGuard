#include "ParseStage.h"
#include "FetchStage.h"
#include "PipelineOrchestrator.h"
#include "Queue.h"
#include "Parser.h"
#include "Logger.h"
#include <stdlib.h>
#include <string.h>

void *ParseStage_Work(void *arg)
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
