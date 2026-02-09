#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "Fetcher.h"
#include "Config.h"
#include "Logger.h"

typedef struct
{
	char *data;
	size_t size;
} WriteBuffer;

// Callback för curl - sparar response data i buffer
static size_t CurlWriteCallback(void *contents, size_t elementSize, size_t elementCount, void *userData)
{
	size_t totalSize = elementSize * elementCount;
	WriteBuffer *buffer = (WriteBuffer *)userData;

	char *newData = realloc(buffer->data, buffer->size + totalSize + 1);
	if (!newData)
		return 0;

	buffer->data = newData;
	memcpy(&buffer->data[buffer->size], contents, totalSize);
	buffer->size += totalSize;
	buffer->data[buffer->size] = 0;

	return totalSize;
}

static int PerformRequest(CURL *curl, const char *url, WriteBuffer *buffer, long *statusCode)
{
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, buffer);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)HTTP_TIMEOUT);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "GridGuard/1.0");

	CURLcode result = curl_easy_perform(curl);
	if (result != CURLE_OK)
		return -1;

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, statusCode);
	return 0;
}

int Fetcher_Initiate(Fetcher *fetcher)
{
	if (!fetcher)
		return -1;

	CURL *curl = curl_easy_init();
	if (!curl)
		return -1;

	fetcher->curl = curl;
	fetcher->isInitialized = true;

	return 0;
}

int Fetcher_Fetch(Fetcher *fetcher, const char *url, FetchResponse *response)
{
	if (!fetcher || !fetcher->isInitialized || !url || !response)
		return -1;

	CURL *curl = (CURL *)fetcher->curl;
	WriteBuffer buffer = {NULL, 0};
	long statusCode = 0;
	int attemptCount = 0;

	// Retry på server errors (5xx)
	while (attemptCount <= HTTP_MAX_RETRIES)
	{
		if (PerformRequest(curl, url, &buffer, &statusCode) == 0)
		{
			// 2xx = success
			if (statusCode >= 200 && statusCode < 300)
			{
				response->data = buffer.data;
				response->size = buffer.size;
				response->status = (int)statusCode;
				return 0;
			}

			// Client errors (4xx) retryar vi inte
			if (statusCode < 500 || attemptCount == HTTP_MAX_RETRIES)
				break;
		}

		attemptCount++;
	}

	free(buffer.data);
	return -1;
}

void Fetcher_FreeResponse(FetchResponse *response)
{
	if (!response)
		return;

	free(response->data);
	response->data = NULL;
	response->size = 0;
	response->status = 0;
}

void Fetcher_Shutdown(Fetcher *fetcher)
{
	if (!fetcher || !fetcher->isInitialized)
		return;

	curl_easy_cleanup((CURL *)fetcher->curl);
	fetcher->curl = NULL;
	fetcher->isInitialized = false;
}
