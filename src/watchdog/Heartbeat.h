#ifndef HEARTBEAT_H
#define HEARTBEAT_H

#define HEARTBEAT_INTERVAL  5   // Process sends heartbeat every 5s
#define HEARTBEAT_TIMEOUT   15  // Watchdog considers process frozen after 15s

typedef struct Heartbeat Heartbeat;

Heartbeat *Heartbeat_Create(void);
void Heartbeat_Destroy(Heartbeat *hb);

// For child process after fork
int Heartbeat_GetWriteFd(const Heartbeat *hb);

// Close appropriate end after fork
int Heartbeat_CloseWriteFd(Heartbeat *hb);  // Parent closes this
int Heartbeat_CloseReadFd(Heartbeat *hb);   // Child closes this

// Returns 1 if heartbeat received, 0 on timeout, -1 on error
int Heartbeat_Check(Heartbeat *hb, int timeout_sec);

#endif
