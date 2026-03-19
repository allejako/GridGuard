#ifndef _PROCESS_HEARTBEAT_H_
#define _PROCESS_HEARTBEAT_H_

#include <time.h>

typedef struct
{
    int    fd;
    time_t lastSent;
    int    interval;
} ProcessHeartbeat;

int ProcessHeartbeat_Initiate(ProcessHeartbeat *heartbeat, int intervalSec);
int ProcessHeartbeat_Send(ProcessHeartbeat *heartbeat);

#endif // _PROCESS_HEARTBEAT_H_
