#include <stdio.h>
#include <string.h>
#include "APIFetcher.h"

int main(void)
{
	APIFetcher fetcher = {0};
	APIResponse response = {0};

	printf("Testing APIFetcher with httpbin.org...\n");

	if (APIFetcher_Initiate(&fetcher) != 0)
	{
		printf("Failed to initiate fetcher\n");
		return 1;
	}

	if (APIFetcher_Fetch(&fetcher, "https://httpbin.org/get", &response) == 0)
	{
		printf("Success! Status: %d, Size: %zu bytes\n", response.status, response.size);
		printf("First 200 chars:\n%.200s\n", response.data);
		APIFetcher_FreeResponse(&response);
		APIFetcher_Shutdown(&fetcher);
		return 0;
	}

	printf("Failed to fetch\n");
	APIFetcher_Shutdown(&fetcher);
	return 1;
}
