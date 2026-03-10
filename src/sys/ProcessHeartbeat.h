#ifndef PROCESS_HEARTBEAT_H
#define PROCESS_HEARTBEAT_H

#include <time.h>

typedef struct {
    int fd;
    time_t lastSent;
    int interval;
} ProcessHeartbeat;

int ProcessHeartbeat_Initiate(ProcessHeartbeat *heartbeat, int intervalSec);
int ProcessHeartbeat_Send(ProcessHeartbeat *heartbeat);

#endif // PROCESS_HEARTBEAT_H
