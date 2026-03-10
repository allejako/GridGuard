#ifndef PROCESS_SPAWNER_H
#define PROCESS_SPAWNER_H

#include "watchdog/Heartbeat.h"
#include <sys/types.h>

// Process lifecycle management for GridGuard pipeline
// Handles fork/exec/heartbeat setup for fetcher, parser, and server processes
// Coordinates startup order to avoid FIFO blocking

typedef struct {
    pid_t pid;
    Heartbeat *heartbeat;
    const char *name;
    const char *path;
} ManagedProcess;

typedef struct {
    ManagedProcess fetcher;
    ManagedProcess parser;
    ManagedProcess server;
} ProcessGroup;

// Initialize process group with binary paths
void ProcessGroup_Init(ProcessGroup *group, const char *fetcherPath, const char *parserPath, const char *serverPath);

// Spawn all processes in coordinated order (Parser → Fetcher → Server)
// Returns 0 on success, -1 if any process fails to spawn
int ProcessGroup_SpawnAll(ProcessGroup *group);

// Send signal to all running processes
void ProcessGroup_KillAll(ProcessGroup *group, int signal);

// Wait for all processes to terminate
void ProcessGroup_WaitAll(ProcessGroup *group);

// Cleanup heartbeats and reset PIDs to -1
void ProcessGroup_Cleanup(ProcessGroup *group);

#endif // PROCESS_SPAWNER_H
