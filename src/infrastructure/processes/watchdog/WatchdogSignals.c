#define _POSIX_C_SOURCE 200809L

#include "WatchdogSignals.h"

#include <signal.h>
#include <string.h>

static void signal_handler(int signum)
{
    if (signum == SIGTERM || signum == SIGINT)
    {
        watchdog_running = 0;

        if (daemon_pid > 0)
            kill(daemon_pid, SIGTERM);
    }
    else if (signum == SIGHUP)
    {
        if (daemon_pid > 0)
            kill(daemon_pid, SIGHUP);
    }
}

void WatchdogSignals_Setup(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGHUP,  &sa, NULL);

    signal(SIGPIPE, SIG_IGN);
}
