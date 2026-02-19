# Ändringslogg - 2026-02-19

## Övergripande förändringar

Komplett daemon- och watchdog-implementation (alla 6 steg). Servern kan nu:

1. Köras som bakgrundsprocess (daemon) med `-d` flagga
2. Övervakas av en separat watchdog-process
3. Automatiskt startas om vid krasch (max 5 gånger på 5 min med exponentiell backoff)
4. Kommunicera via heartbeat-pipe (daemon → watchdog) för att detektera frysta processer
5. Hantera SIGHUP för config reload
6. Köras som systemd-tjänst

Detta täcker kursmålen kring `fork()`, `exec()`, `waitpid()`, `setsid()`, `pipe()`, `select()`, signaler och PID-filer.

---

## Steg 1: Daemon-implementation

### Nya filer

**`src/infrastructure/daemon/Daemon.h`** + **`Daemon.c`**

Implementerar den klassiska 7-stegs Unix-daemoniseringen:

1. `fork()` — parent avslutar med `_exit(0)`
2. `setsid()` — blir session leader, lossnar från terminal
3. `fork()` igen — garanterar att processen aldrig kan återfå en terminal
4. `chdir("/")` — låser inte någon katalog
5. Stänger stdin/stdout/stderr → omdirigerar till `/dev/null`
6. Skriver PID-fil via `PidFile_Write()`
7. Ignorerar `SIGPIPE`

Innehåller även heartbeat-tråd (se steg 5).

**API:**
```c
int Daemon_Init(void);            // Daemonisera processen
int Daemon_StartHeartbeat(void);  // Starta heartbeat-tråd (läser GRIDGUARD_HEARTBEAT_FD)
void Daemon_StopHeartbeat(void);  // Stoppa heartbeat-tråd
void Daemon_Cleanup(void);        // Ta bort PID-fil + stoppa heartbeat
```

**`src/infrastructure/daemon/PidFile.h`** + **`PidFile.c`**

Hanterar PID-filen (`/tmp/gridguard.pid`):

```c
int PidFile_Write(const char *path);        // Skriver nuvarande PID till fil
pid_t PidFile_Read(const char *path);       // Läser PID från fil
int PidFile_Remove(const char *path);       // Tar bort PID-filen
int PidFile_IsRunning(const char *path);    // Kollar om processen lever (kill(pid, 0))
```

---

## Steg 2: Watchdog-implementation

### Nya filer

**`src/infrastructure/watchdog/Watchdog.h`** + **`Watchdog.c`**

Watchdog som startar, övervakar och startar om daemon:

1. Sätter upp signal handlers (SIGTERM/SIGINT/SIGHUP)
2. `fork()` → child: `execl("bin/GridGuard-server", "-d", NULL)`
3. Parent: `waitpid(pid, &status, WNOHANG)` i poll-loop
4. Loggar exit-status (WIFEXITED/WIFSIGNALED med `strsignal`)
5. Vid SIGTERM/SIGINT: forwards `kill(daemon_pid, SIGTERM)` → väntar på clean exit

**`src/infrastructure/watchdog/main.c`**

Entry point för `bin/GridGuard-watchdog`:

```bash
./bin/GridGuard-watchdog                              # Använder default path
./bin/GridGuard-watchdog --daemon-path /path/to/server # Custom path
```

---

## Steg 3: Restart-logik med crash-loop-skydd

Implementerat i `Watchdog.c` via `RestartTracker`:

- **Max 5 omstarter inom 5-minuters fönster**
- **Exponentiell backoff:** 2s → 4s → 8s → 16s → 32s mellan restarts
- Om daemon kör stabilt >5 min → resettas räknaren
- Om gränsen nås → watchdog loggar `FATAL` och avslutar
- Backoff-sleep är interruptible (kollar `watchdog_running` varje sekund)

```c
typedef struct {
    int count;
    time_t first_restart;
    time_t timestamps[MAX_RESTARTS];
} RestartTracker;
```

**Konfigurerbara konstanter:**
```c
#define MAX_RESTARTS        5
#define RESTART_WINDOW_SEC  300   // 5 minuter
#define BASE_BACKOFF_SEC    2
```

---

## Steg 4: SIGHUP signal forwarding

### Watchdog-sidan (`Watchdog.c`)

- Watchdog fångar `SIGHUP` i sin signal handler
- Forwards automatiskt `kill(daemon_pid, SIGHUP)` till daemon
- Loggar: "SIGHUP received, config reload forwarded to daemon"

### Daemon-sidan (`SignalHandler.c/h`)

- `SignalHandler_Init()` registrerar nu även `SIGHUP`-handler
- Sätter `reload_config`-flagga vid mottagning
- Ny funktion `SignalHandler_CheckReload()` — returnerar 1 om SIGHUP mottogs, nollställer flaggan

```c
// Ny API i SignalHandler.h
int SignalHandler_CheckReload(void);
```

**Användning i server-loop (framtida):**
```c
if (SignalHandler_CheckReload()) {
    // Ladda om konfiguration utan restart
}
```

---

## Steg 5: Heartbeat pipe (IPC)

Implementerar inter-processkommunikation via `pipe()` — kursvecka 4.

### Flöde

```
Watchdog                           Daemon
   │                                  │
   ├─ pipe() skapar [read_fd, write_fd]
   ├─ fork()                          │
   │                                  │
   │  ┌─ close(read_fd)              │
   │  ├─ setenv("GRIDGUARD_HEARTBEAT_FD", write_fd)
   │  └─ execl(daemon)               │
   │                                  │
   ├─ close(write_fd)                 ├─ getenv("GRIDGUARD_HEARTBEAT_FD")
   │                                  ├─ Startar heartbeat-tråd
   │                                  │
   │  ┌──────── Loop ────────┐       │  ┌──── Loop ────┐
   │  │ select(read_fd, 2s)  │       │  │ write("heartbeat\n")
   │  │ Om data → OK         │  ◄────│  │ sleep(5)     │
   │  │ Om timeout → frozen! │       │  └──────────────┘
   │  └──────────────────────┘       │
```

### Watchdog-sidan (`Watchdog.c`)

- `Watchdog_CreateHeartbeatPipe()` — skapar `pipe()` före fork
- Child stänger read-end, parent stänger write-end
- `Watchdog_CheckHeartbeat(timeout)` — `select()` med timeout
  - Returnerar `1` (heartbeat OK), `0` (timeout), `-1` (error)
- Vid timeout: `SIGTERM` → vänta 5s → `SIGKILL` → restart

### Daemon-sidan (`Daemon.c`)

- `Daemon_StartHeartbeat()` — läser `GRIDGUARD_HEARTBEAT_FD` från env
- Startar `pthread`-tråd som skriver `"heartbeat\n"` var 5:e sekund
- Om env var saknas (t.ex. server startad manuellt utan watchdog) → no-op
- `Daemon_StopHeartbeat()` — stoppar tråd och stänger fd

**Konfigurerbara konstanter:**
```c
#define HEARTBEAT_INTERVAL  5    // Daemon skriver var 5:e sekund
#define HEARTBEAT_TIMEOUT   15   // Watchdog timeout (miss 3 heartbeats)
#define MONITOR_POLL_SEC    2    // Watchdog poll-intervall
```

---

## Steg 6: Systemd service

**`scripts/gridguard.service`**

Systemd unit-fil för produktion:

```ini
[Unit]
Description=GridGuard Energy Optimization Daemon
After=network.target

[Service]
Type=forking
PIDFile=/tmp/gridguard.pid
ExecStart=/usr/local/bin/GridGuard-watchdog --daemon-path /usr/local/bin/GridGuard-server
Restart=on-failure
RestartSec=10s
```

**Installation:**
```bash
sudo cp scripts/gridguard.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable gridguard
sudo systemctl start gridguard
sudo systemctl status gridguard
```

---

## Ändrade filer

| Fil | Ändring |
|-----|---------|
| `src/server/main.c` | `-d` flagga, absolut log-path, `Daemon_StartHeartbeat()` |
| `src/infrastructure/signals/SignalHandler.c` | SIGHUP handler, `CheckReload()` |
| `src/infrastructure/signals/SignalHandler.h` | Ny funktion `SignalHandler_CheckReload()` |
| `Makefile` | Watchdog binary, targets (`make watchdog`, `make run-watchdog`) |

## Nya filer

```
src/infrastructure/daemon/
├── Daemon.h          # Daemon + heartbeat API
├── Daemon.c          # 7-stegs daemonisering + heartbeat-tråd
├── PidFile.h         # PID-fil API
└── PidFile.c         # PID-fil hantering

src/infrastructure/watchdog/
├── Watchdog.h        # Watchdog API
├── Watchdog.c        # Fork+exec+waitpid, restart, heartbeat pipe
└── main.c            # Watchdog entry point

scripts/
└── gridguard.service # Systemd unit-fil
```

---

## Bygga och testa

```bash
# Bygg
make server           # Server med daemon + heartbeat
make watchdog         # Watchdog binary
make run-watchdog     # Bygger båda och startar

# Test 1: Daemon-mode
./bin/GridGuard-server -d
cat /tmp/gridguard.pid
tail -f logs/server.log
kill $(cat /tmp/gridguard.pid)

# Test 2: Watchdog med restart
./bin/GridGuard-watchdog &
kill -9 $(cat /tmp/gridguard.pid)     # Döda daemon
# Watchdog loggar: "Daemon killed by signal 9"
# Watchdog loggar: "Restarting daemon in 2 seconds (attempt 1/5)"
# Ny daemon spawnas automatiskt

# Test 3: Crash-loop-skydd
# Gör daemon krascha 5 gånger snabbt
# Watchdog: "Max restarts (5) exceeded in 300 seconds, giving up"

# Test 4: Heartbeat
./bin/GridGuard-watchdog
# logs/watchdog.log visar: "Heartbeat pipe created"
# logs/server.log visar: "Heartbeat thread started (fd=X, interval=5s)"

# Test 5: SIGHUP reload
kill -HUP $(pgrep GridGuard-watchdog)
# Watchdog forwards till daemon
# logs/server.log: "SIGHUP received, config reload requested..."

# Test 6: Graceful shutdown
kill $(pgrep GridGuard-watchdog)
# Watchdog: "Waiting for daemon to shut down..."
# Watchdog: "Daemon stopped"
# Watchdog: "Exiting"

# Test 7: Förgrunds-mode (oförändrat)
./bin/GridGuard-server
```

---

## Kursmål som täcks

| Systemanrop | Var det används | Kursvecka |
|-------------|----------------|-----------|
| `fork()` | Daemon.c (2 gånger), Watchdog.c (1 gång per spawn) | Vecka 1 |
| `exec()` / `execl()` | Watchdog.c — startar daemon-binary | Vecka 1 |
| `waitpid()` / `WNOHANG` | Watchdog.c — non-blocking monitoring | Vecka 1 |
| `setsid()` | Daemon.c — blir session leader | Vecka 1 |
| `_exit()` | Daemon.c — parent exits efter fork | Vecka 1 |
| `pipe()` | Watchdog.c — heartbeat-pipe före fork | Vecka 4 |
| `select()` | Watchdog.c — non-blocking pipe read med timeout | Vecka 4 |
| `read()` / `write()` | Heartbeat: daemon skriver, watchdog läser | Vecka 4 |
| `setenv()` / `getenv()` | Skicka heartbeat fd via environment | Vecka 1 |
| `kill()` | Watchdog.c — SIGTERM/SIGKILL/SIGHUP till daemon | Vecka 5 |
| `signal()` / `sigaction()` | SIGTERM, SIGINT, SIGHUP, SIGPIPE | Vecka 5 |
| `chdir()` | Daemon.c — byter till `/` | Vecka 1 |
| `open()` / `dup2()` / `close()` | Daemon.c — omdirigerar fd till `/dev/null` | Vecka 1 |
| `pthread_create()` | Daemon.c — heartbeat-tråd | Vecka 2 |
| PID-filer | PidFile.c — process management | Vecka 1 |

---

## Binaries

```
bin/GridGuard-server    (288 KB)  # Daemon med -d flagga + heartbeat
bin/GridGuard-watchdog  (36 KB)   # Watchdog med restart + heartbeat pipe
```

Alla filer kompilerar med `-Wall -Wextra -Werror` utan varningar.
