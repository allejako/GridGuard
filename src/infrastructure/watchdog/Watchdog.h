#ifndef WATCHDOG_H
#define WATCHDOG_H

// Run the watchdog process. This is the main entry point.
// Forks a child, execs the daemon binary, and monitors it with waitpid.
// Blocks until the daemon exits or watchdog receives SIGTERM/SIGINT.
//
// daemon_path: path to the GridGuard-server binary (e.g. "bin/GridGuard-server")
// Returns: 0 on clean shutdown, 1 on daemon crash
int Watchdog_Run(const char *daemon_path);

#endif // WATCHDOG_H
