#ifndef DAEMON_H
#define DAEMON_H

// Daemonize the current process using the classic 7-step Unix procedure:
// 1. fork() - parent exits
// 2. setsid() - become session leader
// 3. fork() again - can't reacquire terminal
// 4. chdir("/") - don't lock directories
// 5. Close stdin/stdout/stderr, redirect to /dev/null
// 6. Write PID file
// 7. Ignore SIGPIPE
//
// Returns 0 on success (in the final daemon child), -1 on failure.
// After this call, the process is fully detached from any terminal.
int Daemon_Initiate(void);

// Start the heartbeat thread. Reads GRIDGUARD_HEARTBEAT_FD from environment
// and writes "heartbeat\n" every 5 seconds to that fd.
// If the env var is not set, this is a no-op (returns 0).
// Returns 0 on success, -1 on failure.
int Daemon_StartHeartbeat(void);

// Stop the heartbeat thread and close the heartbeat fd.
void Daemon_StopHeartbeat(void);

// Clean up daemon resources (remove PID file). Call during shutdown.
void Daemon_Shutdown(void);

#endif // DAEMON_H
