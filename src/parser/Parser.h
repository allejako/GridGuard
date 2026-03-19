#ifndef _PARSER_PROCESS_H_
#define _PARSER_PROCESS_H_

#include <stddef.h>
#include <stdbool.h>
#include <time.h>

time_t parse_iso8601(const char *timeStr, int utc_offset_seconds);

// Dedicated parser process that runs as its own executable via exec().
// Reads FetchResult from FIFO (named pipe from Fetch process).
// Serves a Unix domain socket where the Compute thread connects as client.
// Sends ParseResult to Compute via socket.

typedef struct
{
    int fifoFd;              // Read end of FIFO from fetch process
    int serverSocket;        // Unix domain socket server
    int notifyFd;            // Write end of notify FIFO to Compute
    char fifoPath[256];      // Path to FIFO file
    char socketPath[256];    // Path to Unix socket
    char notifyPath[256];    // Path to notify FIFO
    void *parser;            // Parser service (opaque pointer)
    bool isRunning;
} ParserProcess;

int  ParserProcess_Initiate(ParserProcess *proc, const char *fifoPath, const char *socketPath, const char *notifyPath);
int  ParserProcess_Run(ParserProcess *proc);
void ParserProcess_Shutdown(ParserProcess *proc);

#endif // _PARSER_PROCESS_H_
