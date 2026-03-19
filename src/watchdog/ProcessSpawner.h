#ifndef _PROCESS_SPAWNER_H_
#define _PROCESS_SPAWNER_H_

#include "watchdog/Heartbeat.h"
#include <sys/types.h>

typedef struct
{
    pid_t       pid;
    Heartbeat   heartbeat;
    int         heartbeatInitialized;
    const char *name;
    const char *path;
} ManagedProcess;

typedef struct
{
    ManagedProcess fetcher;
    ManagedProcess parser;
    ManagedProcess server;
} ProcessGroup;

void ProcessGroup_Initiate(ProcessGroup *group, const char *fetcherPath, const char *parserPath, const char *serverPath);
int  ProcessGroup_SpawnAll(ProcessGroup *group);
void ProcessGroup_KillAll(ProcessGroup *group, int signal);
void ProcessGroup_WaitAll(ProcessGroup *group);
void ProcessGroup_Cleanup(ProcessGroup *group);

#endif // _PROCESS_SPAWNER_H_
