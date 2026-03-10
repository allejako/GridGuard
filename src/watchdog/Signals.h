#ifndef SIGNALS_H
#define SIGNALS_H

#include <signal.h>
#include <sys/types.h>

// Shared between signal handler and main loop
extern volatile sig_atomic_t watchdog_running;      // SIGTERM/SIGINT
extern volatile sig_atomic_t log_process_status;    // SIGUSR1
extern volatile sig_atomic_t manual_restart;        // SIGUSR2
extern volatile pid_t        fetcher_pid;
extern volatile pid_t        parser_pid;
extern volatile pid_t        server_pid;

void Signals_Initiate(void);

#endif
