#ifndef _FETCHER_PROCESS_H_
#define _FETCHER_PROCESS_H_

#include <stddef.h>
#include <stdbool.h>

// Dedicated fetch process, runs as its own executable via exec().
// Reads WorkRequests from stdin (pipe from main process).
// Writes FetchResults to a FIFO (named pipe to the parse process).
// Uses SharedCache to cache weather/price data between processes.

typedef struct
{
    int stdinFd;           // read end of pipe from main process
    int fifoFd;            // write end of FIFO to parse process
    char fifoPath[256];
    void *fetcher;         // Fetcher service (opaque pointer)
    void *weatherCache;    // SharedCache for weather data
    void *priceCache;      // SharedCache for price data
    bool isRunning;
} FetcherProcess;

int  FetcherProcess_Initiate(FetcherProcess *proc, const char *fifoPath);
int  FetcherProcess_Run(FetcherProcess *proc);
void FetcherProcess_Shutdown(FetcherProcess *proc);

#endif // _FETCHER_PROCESS_H_
