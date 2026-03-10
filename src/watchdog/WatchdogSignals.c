#define _POSIX_C_SOURCE 200809L

#include "watchdog/WatchdogSignals.h"

#include <signal.h>
#include <string.h>

static void signal_handler(int signum)
{
    if (signum == SIGTERM || signum == SIGINT)
    {
        watchdog_running = 0;

        if (fetcher_pid > 0)
            kill(fetcher_pid, SIGTERM);
        if (parser_pid > 0)
            kill(parser_pid, SIGTERM);
        if (server_pid > 0)
            kill(server_pid, SIGTERM);
    }
    else if (signum == SIGHUP)
    {
        if (fetcher_pid > 0)
            kill(fetcher_pid, SIGHUP);
        if (parser_pid > 0)
            kill(parser_pid, SIGHUP);
        if (server_pid > 0)
            kill(server_pid, SIGHUP);
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
