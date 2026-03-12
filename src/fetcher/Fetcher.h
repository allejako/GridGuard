#ifndef _FETCHER_PROCESS_H_
#define _FETCHER_PROCESS_H_

#include <stddef.h>
#include <stdbool.h>
#include <time.h>

// Dedicated fetch process, runs as its own executable via exec().
// Reads WorkRequests from stdin (pipe from main process).
// Writes FetchResults to a FIFO (named pipe to the parse process).
// Uses SharedCache to cache weather/price data between processes.

typedef struct
{
    int requestFifoFd;     // read end of request FIFO from server
    int resultFifoFd;      // write end of result FIFO to parser
    char requestFifoPath[256];
    char resultFifoPath[256];
    void *fetcher;         // Fetcher service (opaque pointer)
    void *weatherCache;    // SharedCache for weather data
    void *priceCache;      // SharedCache for price data
    bool isRunning;

    // Circuit breaker state for weather API
    int weatherFailures;
    time_t weatherBackoffUntil;

    // Circuit breaker state for price API
    int priceFailures;
    time_t priceBackoffUntil;
} FetcherProcess;

int  FetcherProcess_Initiate(FetcherProcess *proc, const char *requestFifoPath, const char *resultFifoPath);
int  FetcherProcess_Run(FetcherProcess *proc);
void FetcherProcess_Shutdown(FetcherProcess *proc);

#endif // _FETCHER_PROCESS_H_
