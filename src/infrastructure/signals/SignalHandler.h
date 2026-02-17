#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

#include <signal.h>

// Initialize signal handlers for server shutdown (SIGINT, SIGTERM)
// Returns pointer to the keep_running flag
volatile sig_atomic_t *SignalHandler_Init(void);

// Set the server socket fd to close on shutdown signal
void SignalHandler_SetServerFd(int fd);

#endif // SIGNAL_HANDLER_H
