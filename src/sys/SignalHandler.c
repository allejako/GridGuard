#define _POSIX_C_SOURCE 200809L

#include "sys/SignalHandler.h"
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

static volatile sig_atomic_t keepRunning  = 1;
static volatile sig_atomic_t reloadConfig = 0;
static int                   serverFd     = -1;

// Signal-safe message writing
static void WriteMessage(const char *msg)
{
    write(STDOUT_FILENO, msg, strlen(msg));
}

static void HandleSignal(int signum)
{
    if (signum == SIGINT || signum == SIGTERM)
    {
        WriteMessage("\nSignal received, shutting down server...\n");
        keepRunning = 0;
        if (serverFd >= 0) {
            close(serverFd);
            serverFd = -1;
        }
    }
    else if (signum == SIGHUP)
    {
        WriteMessage("\nSIGHUP received, config reload requested...\n");
        reloadConfig = 1;
    }
}

int SignalHandler_Initiate(void)
{
    struct sigaction sa;

    sa.sa_handler = HandleSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP,  &sa, NULL);

    // Ignore SIGPIPE - prevents crash when clients disconnect
    signal(SIGPIPE, SIG_IGN);

    keepRunning  = 1;
    reloadConfig = 0;
    return 0;
}

void SignalHandler_Shutdown(void)
{
    keepRunning  = 1;
    reloadConfig = 0;
    serverFd     = -1;
}

int SignalHandler_IsRunning(void)
{
    return (int)keepRunning;
}

void SignalHandler_SetServerFd(int fd)
{
    serverFd = fd;
}

int SignalHandler_CheckReload(void)
{
    if (reloadConfig)
    {
        reloadConfig = 0;
        return 1;
    }
    return 0;
}
