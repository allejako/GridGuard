#define _POSIX_C_SOURCE 200809L

#include "watchdog/Signals.h"
#include <signal.h>
#include <string.h>

volatile sig_atomic_t log_process_status = 0;
volatile sig_atomic_t manual_restart = 0;

// Signal handler for watchdog process coordination
// All handlers use async-signal-safe operations only (setting flags, calling kill)
static void signal_handler(int signum)
{
    if (signum == SIGTERM || signum == SIGINT)
    {
        // Graceful shutdown: stop monitoring and terminate child processes
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
        // Config reload: forward signal to all child processes
        if (fetcher_pid > 0)
            kill(fetcher_pid, SIGHUP);
        if (parser_pid > 0)
            kill(parser_pid, SIGHUP);
        if (server_pid > 0)
            kill(server_pid, SIGHUP);
    }
    else if (signum == SIGUSR1)
    {
        // Status report: log PIDs and heartbeat times without restarting
        log_process_status = 1;
    }
    else if (signum == SIGUSR2)
    {
        // Force restart: kill and respawn all processes immediately
        manual_restart = 1;
    }
}

// Install signal handlers for watchdog lifecycle management
void Signals_Initiate(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGHUP,  &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);

    // Ignore broken pipe (write to closed FIFO won't crash process)
    signal(SIGPIPE, SIG_IGN);
}
