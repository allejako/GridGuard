#define _POSIX_C_SOURCE 200809L

#include "SignalHandler.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>

static volatile sig_atomic_t keep_running = 1;
static int server_fd = -1;

// Signal-safe message writing
static void SignalHandler_Write(const char *msg)
{
    write(STDOUT_FILENO, msg, strlen(msg));
}

static void SignalHandler_HandleSignal(int signum) 
{
    if (signum == SIGINT || signum == SIGTERM) 
    {
        SignalHandler_Write("\nSignal received, shutting down server...\n");
        keep_running = 0;
        if (server_fd >= 0) {
            close(server_fd);
            server_fd = -1;
        }
    }
}

volatile sig_atomic_t *SignalHandler_Init(void)
{
    struct sigaction sa;

    sa.sa_handler = SignalHandler_HandleSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    return &keep_running;
}

void SignalHandler_SetServerFd(int fd)
{
    server_fd = fd;
}
