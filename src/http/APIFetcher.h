#ifndef _APIFETCHER_H_
#define _APIFETCHER_H_

#include <stddef.h>
#include <stdbool.h>

typedef struct
{
	void *curl;
	bool isInitialized;
} APIFetcher;

typedef struct
{
	char *data;
	size_t size;
	int status;
} APIResponse;

int APIFetcher_Initiate(APIFetcher *fetcher);
int APIFetcher_Fetch(APIFetcher *fetcher, const char *url, APIResponse *response);
void APIFetcher_FreeResponse(APIResponse *response);
void APIFetcher_Shutdown(APIFetcher *fetcher);

#endif
