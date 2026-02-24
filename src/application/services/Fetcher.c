#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>

#include "Fetcher.h"
#include "HTTPClient.h"
#include "Config.h"
#include "Logger.h"

int Fetcher_Initiate(Fetcher *fetcher)
{
    if (!fetcher)
        return -1;

    if (HTTPClient_Initiate(&fetcher->httpClient) != 0)
        return -1;

    fetcher->isInitialized = true;
    pthread_mutex_init(&fetcher->mutex, NULL);

    return 0;
}

int Fetcher_Fetch(Fetcher *fetcher, const char *url, FetchResponse *response)
{
    if (!fetcher || !fetcher->isInitialized || !url || !response)
        return -1;

    pthread_mutex_lock(&fetcher->mutex);

    int attemptCount = 0;

    while (attemptCount <= HTTP_MAX_RETRIES)
    {
        HTTPClientResponse httpResp = {0};

        if (HTTPClient_Get(&fetcher->httpClient, url, &httpResp, HTTP_TIMEOUT) == 0)
        {
            if (httpResp.statusCode >= 200 && httpResp.statusCode < 300)
            {
                response->data   = httpResp.body;
                response->size   = httpResp.bodyLen;
                response->status = httpResp.statusCode;
                pthread_mutex_unlock(&fetcher->mutex);
                return 0;
            }

            // 4xx — retry hjälper inte
            if (httpResp.statusCode < 500 || attemptCount == HTTP_MAX_RETRIES)
            {
                HTTPClient_FreeResponse(&httpResp);
                break;
            }

            HTTPClient_FreeResponse(&httpResp);
        }

        attemptCount++;
    }

    pthread_mutex_unlock(&fetcher->mutex);
    return -1;
}

void Fetcher_FreeResponse(FetchResponse *response)
{
    if (!response)
        return;

    free(response->data);
    response->data   = NULL;
    response->size   = 0;
    response->status = 0;
}

void Fetcher_Shutdown(Fetcher *fetcher)
{
    if (!fetcher || !fetcher->isInitialized)
        return;

    pthread_mutex_destroy(&fetcher->mutex);
    HTTPClient_Shutdown(&fetcher->httpClient);
    fetcher->isInitialized = false;
}
