#ifndef STATUS_H
#define STATUS_H

// Non-blocking FIFO for streaming watchdog events to monitoring tools
typedef struct Status Status;

Status *Status_Initiate(const char *fifo_path);
void Status_Write(Status *status, const char *fmt, ...);
void Status_Shutdown(Status *status);

#endif
