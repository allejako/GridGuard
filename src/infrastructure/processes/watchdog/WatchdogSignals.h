#ifndef WATCHDOG_SIGNALS_H
#define WATCHDOG_SIGNALS_H

#include <signal.h>
#include <sys/types.h>

// Definieras i Watchdog.c — exponeras så att signalhanteraren kan nå dem.
extern volatile sig_atomic_t watchdog_running;
extern volatile pid_t        daemon_pid;

void WatchdogSignals_Setup(void);

#endif // WATCHDOG_SIGNALS_H
