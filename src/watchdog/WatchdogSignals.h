#ifndef WATCHDOG_SIGNALS_H
#define WATCHDOG_SIGNALS_H

#include <signal.h>
#include <sys/types.h>

extern volatile sig_atomic_t watchdog_running;
extern volatile pid_t        fetcher_pid;
extern volatile pid_t        parser_pid;
extern volatile pid_t        server_pid;

void WatchdogSignals_Setup(void);

#endif // WATCHDOG_SIGNALS_H
