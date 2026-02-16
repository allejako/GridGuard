#ifndef _CLIENT_HANDLER_H_
#define _CLIENT_HANDLER_H_

#include <stdbool.h>
#include "Config.h"

// Forward declaration
struct Pipeline;

// Client connection states
typedef enum {
    CLIENT_DISCONNECTED = 0,
    CLIENT_CONNECTED,
    CLIENT_AUTHENTICATING,
    CLIENT_READY,
    CLIENT_PROCESSING
} ClientState;

// Client connection data
typedef struct {
    int fd;
    ClientState state;
    char buffer[CLIENT_BUFFER_SIZE];
    int bufferLen;
} Client;

// Handle client state machine - processes commands and updates state
void Client_HandleState(Client *client, struct Pipeline *pipeline);

#endif // _CLIENT_HANDLER_H_
