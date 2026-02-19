#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

#include <signal.h>

// Initialize signal handlers for server shutdown (SIGINT, SIGTERM)
// and config reload (SIGHUP).
// Returns pointer to the keep_running flag
volatile sig_atomic_t *SignalHandler_Init(void);

// Set the server socket fd to close on shutdown signal
void SignalHandler_SetServerFd(int fd);

// Check and clear the reload_config flag. Returns 1 if SIGHUP was received.
int SignalHandler_CheckReload(void);

#endif // SIGNAL_HANDLER_H
