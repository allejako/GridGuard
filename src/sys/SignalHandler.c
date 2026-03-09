#define _POSIX_C_SOURCE 200809L

#include "sys/SignalHandler.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>

static volatile sig_atomic_t keep_running = 1;
static volatile sig_atomic_t reload_config = 0;
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
    else if (signum == SIGHUP)
    {
        SignalHandler_Write("\nSIGHUP received, config reload requested...\n");
        reload_config = 1;
    }
}

volatile sig_atomic_t *SignalHandler_Initiate(void)
{
    struct sigaction sa;

    sa.sa_handler = SignalHandler_HandleSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    // Ignore SIGPIPE - prevents crash when clients disconnect
    signal(SIGPIPE, SIG_IGN);

    return &keep_running;
}

void SignalHandler_SetServerFd(int fd)
{
    server_fd = fd;
}

int SignalHandler_CheckReload(void)
{
    if (reload_config)
    {
        reload_config = 0;
        return 1;
    }
    return 0;
}
