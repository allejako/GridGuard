#ifndef PIDFILE_H
#define PIDFILE_H

#include <sys/types.h>

#define GRIDGUARD_PID_FILE "/tmp/gridguard.pid"

// Write current process PID to file. Returns 0 on success, -1 on failure.
int PidFile_Write(const char *path);

// Read PID from file. Returns the PID or -1 on failure.
pid_t PidFile_Read(const char *path);

// Remove PID file. Returns 0 on success, -1 on failure.
int PidFile_Remove(const char *path);

// Check if the process in PID file is still running. Returns 1 if running, 0 if not.
int PidFile_IsRunning(const char *path);

#endif // PIDFILE_H
