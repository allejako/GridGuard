#ifndef _SIGNAL_HANDLER_H_
#define _SIGNAL_HANDLER_H_

// Initialize signal handlers for server shutdown (SIGINT, SIGTERM)
// and config reload (SIGHUP).
// Returns 0 on success, -1 on failure.
int  SignalHandler_Initiate(void);
void SignalHandler_Shutdown(void);

// Returns 1 if the server should keep running, 0 if shutdown was requested.
int  SignalHandler_IsRunning(void);

// Set the server socket fd to close on shutdown signal.
void SignalHandler_SetServerFd(int fd);

// Check and clear the reload_config flag. Returns 1 if SIGHUP was received.
int  SignalHandler_CheckReload(void);

#endif // _SIGNAL_HANDLER_H_
