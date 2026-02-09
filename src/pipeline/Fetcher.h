#ifndef _FETCHER_H_
#define _FETCHER_H_

#include <stddef.h>
#include <stdbool.h>

typedef struct
{
	void *curl;
	bool isInitialized;
} Fetcher;

typedef struct
{
	char *data;
	size_t size;
	int status;
} FetchResponse;

int Fetcher_Initiate(Fetcher *fetcher);
int Fetcher_Fetch(Fetcher *fetcher, const char *url, FetchResponse *response);
void Fetcher_FreeResponse(FetchResponse *response);
void Fetcher_Shutdown(Fetcher *fetcher);

#endif
