#define _POSIX_C_SOURCE 200809L

#include "SignalHandler.h"
#include <stdio.h>
#include <unistd.h>

static volatile sig_atomic_t keep_running = 1;
static int server_fd = -1;

static void SignalHandler_HandleSignal(int signum) 
{
    if (signum == SIGINT || signum == SIGTERM) 
    {
        printf("\nSignal %d received, shutting down server\n", signum);
        keep_running = 0;
        if (server_fd >= 0) {
            close(server_fd);
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
