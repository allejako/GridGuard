# Ändringslogg - 2026-03-10

---

## Watchdog som supervisor — övergång från server-spawning

Vi har gått från ett system där Server spawnar och hanterar Fetcher och Parser själv, till ett där Watchdog fungerar som en supervisor som startar och övervakar alla tre processer.

---

## Problemet vi hade

I `development`-branchen såg arkitekturen ut såhär:

```
Server (main process)
├── Skapar FIFO och Unix socket
├── Skapar anonymous pipe
├── fork() → Fetcher
│   └── Läser från stdin (pipen)
├── fork() → Parser
└── pthread_create() → ComputeWorker
```

**Problem:**
1. **Ingen crash recovery** — Om Fetcher eller Parser kraschade upptäckte vi det inte förrän nästa request hängde
2. **Tight coupling** — Server ansvarade för att spawna children, hantera IPC-resurser OCH serva HTTP
3. **Race conditions** — Måste använda `sleep(1)` mellan fork-operationer för att säkerställa att FIFO-ändarna öppnades i rätt ordning
4. **Ingen freeze detection** — En process som hängde i deadlock eller infinite loop upptäcktes aldrig

---

## Lösningen: Watchdog som supervisor

```
Watchdog (supervisor)
├── Skapar ALLA IPC-resurser innan spawn
│   ├── /tmp/gridguard_requests.fifo
│   ├── /tmp/gridguard_fetch_to_parse.fifo
│   └── /tmp/gridguard_parse_to_compute.sock
├── Spawnar processer i rätt ordning
│   ├── Parser först (öppnar FIFO read end)
│   ├── Fetcher (öppnar FIFO write end)
│   └── Server (HTTP + ComputeWorker thread)
└── Monitrar alla tre via heartbeats
    ├── Parser skickar "hb" var 5:e sekund
    ├── Fetcher skickar "hb" var 5:e sekund
    └── Server skickar "hb" var 5:e sekund
```

**Vad händer vid problem:**
- **Crash** (segfault, abort): Watchdog får SIGCHLD, loggar exit code, startar om alla tre processer
- **Freeze** (deadlock, infinite loop): Watchdog ser att heartbeat timeout (15s), dödar och startar om
- **Graceful exit**: Watchdog loggar, cleanup IPC-resurser, exit

---

## ProcessHeartbeat (`src/sys/ProcessHeartbeat.c/h`)

Enkel modul för att skicka heartbeats från child-processer tillbaka till Watchdog via en pipe som skickas via miljövariabeln `GRIDGUARD_HEARTBEAT_FD`.

```c
// I child-process
ProcessHeartbeat heartbeat;
ProcessHeartbeat_Initiate(&heartbeat, 5);  // 5 sekunders interval

while (running) {
    ProcessHeartbeat_Send(&heartbeat);
    // ... gör arbete ...
}
```

Watchdog pollar pipe:n var 2:a sekund. Om ingen heartbeat kommit inom 15 sekunder klassas processen som fryst.

**Varför inte bara waitpid()?** waitpid() upptäcker bara crashes. Heartbeat upptäcker även freeze (process lever men gör inget).

---

## Restart-policy med exponential backoff

Exponential backoff: 2s → 4s → 8s → 16s → 32s. Max 5 restarts på 5 minuter. Counter resettas automatiskt när fönstret gått ut, så tillfälliga problem triggar inte permanent shutdown.

Koden finns i `src/watchdog/RestartPolicy.c`.

---

## Signaler för live-debugging

- `SIGUSR1` — dumpar process-status i loggen (PID, heartbeat-timestamps, restart-counter) utan att störa systemet
- `SIGUSR2` — tvingar omstart av alla processer direkt

Finns i `src/watchdog/WatchdogSignals.h/c`.

---

## Metrics endpoint

`/metrics` är öppen utan auth. Använder shared memory (`/gridguard_watchdog_metrics`) så server kan läsa watchdog-data utan IPC-calls. Response innehåller PIDs, heartbeat-timestamps och restart-statistik.

---

## Forecast cache tillagd

Ny cache för färdiga JSON-responses som kompletterar weather/price caches:

```
forecastCache  ← Färdiga JSON-responses (15 min TTL)
    ↓
weatherCache   ← Rådata från OpenMeteo
priceCache     ← Rådata från Elpriset
```

Cache hit ger 400–1000× snabbare svar jämfört med att köra hela pipelinen.

---

## Refaktorering: Watchdog delad i moduler

Watchdog.c var ~600 rader och innehöll allt från process spawning till IPC-hantering. Bröt ut i fem fokuserade moduler:

- `IPC.c` — skapar och städar upp FIFOs
- `Status.c` — non-blocking FIFO för watchdog-events
- `ProcessSpawner.c` — process-hantering med `ProcessGroup` struct
- `Signals.c` — SIGUSR1/SIGUSR2 signal handlers
- `Metrics.c` — shared memory för metrics

`Watchdog.c`: 600 rader → 342 rader. Fokuserar nu enbart på monitoring loop och restart-logik.

---

## Main launcher

Skapade `src/main.c` som fungerar som systemets entry point. Gör bara `execv()` till watchdog — ett tydligt entry point istället för att användare ska behöva veta att de ska köra `bin/GridGuard-watchdog`.

---

## Test Infrastructure: Google Test + ASAN/UBSAN

Gamla C-tester (`src/tests/unit/`) körde med `assert()` och printade "PASS"/"FAIL" till stdout. Ingen sanitizers, ingen concurrency-testing.

Ny test-stack:

```
CMake 3.14+
├── FetchContent (hämtar Google Test 1.14.0 automatiskt)
├── ASAN + UBSAN (aktiverade i Debug builds)
└── 6 test-suites — 60 tester totalt
    ├── test_logger_gtest          (7 tester)
    ├── test_http_request_gtest    (8 tester)
    ├── test_http_response_gtest   (13 tester)
    ├── test_restart_policy_gtest  (11 tester — watchdog resilience)
    ├── test_scheduler_gtest       (14 tester — energy optimization)
    └── test_queue_concurrent_gtest (16 tester — concurrency stress)
```

**Total:** 60 tester, 100% pass med ASAN/UBSAN enabled.

---

## GridGuard-specifika tester

### RestartPolicy (11 tester)

Testar exponential backoff och rate limiting — watchdog kärn-resilience.

```cpp
TEST_F(RestartPolicyTest, ExponentialBackoffProgression) {
    policy = RestartPolicy_Create(5, 300, 2);
    RestartPolicy_RecordRestart(policy);
    EXPECT_EQ(RestartPolicy_GetBackoffDelay(policy), 2);
    RestartPolicy_RecordRestart(policy);
    EXPECT_EQ(RestartPolicy_GetBackoffDelay(policy), 4);
    RestartPolicy_RecordRestart(policy);
    EXPECT_EQ(RestartPolicy_GetBackoffDelay(policy), 8);
}
```

### LoadScheduler (14 tester)

Testar spot price optimization med realistiska scenarios — EV-laddning, deadline constraints, besparingsberäkning.

### Queue Concurrent (16 tester)

Stress-testar thread-safe work distribution: 8 producers + 8 consumers, 400 items, data integrity verifierad.

---

## ASAN hittade faktiskt en bugg

Under utvecklingen hittade Address Sanitizer en heap buffer overflow i RestartPolicy:

```
==126345==ERROR: AddressSanitizer: heap-buffer-overflow
WRITE of size 8 at 0x506000000900 thread T0
    #0 RestartPolicy_RecordRestart /src/watchdog/RestartPolicy.c:58
```

`timestamps[MAX_RESTARTS]` hade 5 element, testet försökte göra 6 restarts. Fixades genom att begränsa testet. Utan ASAN hade vi inte hittat detta förrän watchdog kraschade i produktion.

---

## Testresultat

```bash
$ make test-gtest
100% tests passed, 0 tests failed out of 60
Total Test time (real) = 1.61 sec
```

Med ASAN/UBSAN aktiverat. Inga memory leaks, inga buffer overflows, inga race conditions.

---

## Hur man kör testerna

```bash
make test-gtest                              # Bygg och kör alla 60 tester
./build/tests/test_restart_policy_gtest      # Watchdog resilience
./build/tests/test_scheduler_gtest           # Energy optimization
./build/tests/test_queue_concurrent_gtest    # Concurrency stress
```
