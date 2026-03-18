#ifndef _PARSER_PROCESS_H_
#define _PARSER_PROCESS_H_

#include <stddef.h>
#include <stdbool.h>
#include <time.h>

time_t parse_iso8601(const char *timeStr, int utc_offset_seconds);

// Dedikerad parse-process som körs som egen executable via exec().
// Läser FetchResult från FIFO (named pipe från Fetch process).
// Serverar Unix domain socket där Compute-tråden connectar som client.
// Skickar ParseResult till Compute via socket.

typedef struct
{
    int fifoFd;              // Read end av FIFO från fetch process
    int serverSocket;        // Unix domain socket server
    int notifyFd;            // Write end av notify FIFO till Compute
    char fifoPath[256];      // Path till FIFO-filen
    char socketPath[256];    // Path till Unix socket
    char notifyPath[256];    // Path till notify FIFO
    void *parser;            // Parser service (opaque pointer)
    bool isRunning;
} ParserProcess;

int  ParserProcess_Initiate(ParserProcess *proc, const char *fifoPath, const char *socketPath, const char *notifyPath);
int  ParserProcess_Run(ParserProcess *proc);
void ParserProcess_Shutdown(ParserProcess *proc);

#endif // _PARSER_PROCESS_H_
