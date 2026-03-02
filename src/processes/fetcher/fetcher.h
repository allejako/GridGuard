#ifndef _FETCHER_PROCESS_H_
#define _FETCHER_PROCESS_H_

#include <stddef.h>
#include <stdbool.h>

// Dedikerad fetch-process som körs som egen executable via exec().
// Läser WorkRequest från stdin (pipe från main process).
// Skriver FetchResult till FIFO (named pipe till Parse process).
// Använder SharedCache för att cacha weather/price data mellan processer.

typedef struct
{
    int stdinFd;           // Read end av pipe från main process
    int fifoFd;            // Write end av FIFO till parse process
    char fifoPath[256];    // Path till FIFO-filen
    void *fetcher;         // Fetcher service (opaque pointer)
    void *weatherCache;    // SharedCache för weather data
    void *priceCache;      // SharedCache för price data
    bool isRunning;
} FetcherProcess;

int  FetcherProcess_Initiate(FetcherProcess *proc, const char *fifoPath);
int  FetcherProcess_Run(FetcherProcess *proc);
void FetcherProcess_Shutdown(FetcherProcess *proc);

#endif // _FETCHER_PROCESS_H_
