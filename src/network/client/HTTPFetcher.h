#ifndef _HTTP_FETCHER_H_
#define _HTTP_FETCHER_H_

#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
#include "HTTPClient.h"

typedef struct
{
    bool            isInitialized;
    pthread_mutex_t mutex;
    HTTPClient      httpClient;
} HTTPFetcher;

typedef struct
{
    char  *data;
    size_t size;
    int    status;
} HTTPFetchResponse;

int  HTTPFetcher_Initiate(HTTPFetcher *fetcher);
int  HTTPFetcher_Fetch(HTTPFetcher *fetcher, const char *url, HTTPFetchResponse *response);
void HTTPFetcher_FreeResponse(HTTPFetchResponse *response);
void HTTPFetcher_Shutdown(HTTPFetcher *fetcher);

#endif
