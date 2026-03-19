# GridGuard Test Infrastructure

Professional test suite designed specifically for GridGuard's unique architecture: **resilient process supervision**, **energy-aware scheduling**, and **distributed work distribution**.

## Test Philosophy

GridGuard is not a typical web service - it's a **resilient distributed system** with watchdog supervision and energy optimization. Our tests reflect this:

- **Resilience over Correctness**: We test how the system *recovers* from failures, not just if it works
- **Concurrency First**: All critical paths are tested under multi-threaded stress
- **Domain-Driven**: Tests focus on energy optimization logic - GridGuard's core value proposition
- **Production Scenarios**: Real-world cases (EV charging, appliance scheduling, process crashes)

## Architecture

```
tests/
├── unit/                                   # Unit tests with Google Test
│   ├── test_logger_gtest.cpp              # Basic infrastructure
│   ├── test_http_request_gtest.cpp
│   ├── test_http_response_gtest.cpp
│   ├── test_config_parser_gtest.cpp
│   │
│   ├── test_restart_policy_gtest.cpp      # WATCHDOG RESILIENCE
│   ├── test_scheduler_gtest.cpp           # ENERGY OPTIMIZATION
│   ├── test_queue_concurrent_gtest.cpp    # CONCURRENCY STRESS
│   │
│   ├── test_jwt_gtest.cpp                 # TOKEN LIFECYCLE
│   ├── test_runtime_config_gtest.cpp      # CONFIG FALLBACK CHAIN
│   ├── test_api_endpoints_gtest.cpp       # URL CONSTRUCTION
│   ├── test_compute_gtest.cpp             # BUY/SELL/AVOID/IDLE SIGNALS
│   ├── test_work_completion_gtest.cpp     # SYNC PRIMITIVE
│   ├── test_heartbeat_gtest.cpp           # WATCHDOG IPC
│   ├── test_schedule_db_gtest.cpp         # SQLITE PERSISTENCE
│   └── test_user_config_db_gtest.cpp      # SQLITE PERSISTENCE
│
├── integration/                    # Integration & chaos tests
│   ├── test_process_pipeline_gtest.cpp   # MULTI-PROCESS IPC
│   ├── test_chaos_watchdog_gtest.cpp     # SIGKILL CHAOS
│   ├── test_api_fetch.c                  # Legacy C: API live calls
│   ├── test_openmeteo_parser.c           # Legacy C: parser
│   └── test_pipeline.c                  # Legacy C: in-process pipeline
│
└── benchmarks/                     # Performance benchmarks
    ├── bench_compute.c
    ├── bench_queue.c
    └── bench_cache.c
```

## GridGuard-Specific Test Suites

### 1. Watchdog Resilience Tests (`test_restart_policy_gtest.cpp`)

Tests GridGuard's core supervision capabilities:

- **Exponential Backoff**: Validates 2s → 4s → 8s → 16s → 32s progression
- **Rate Limiting**: Prevents restart storms when processes crash repeatedly
- **Realistic Crash Scenarios**: Simulates server failures and recovery

**Why This Matters**: GridGuard's watchdog is the foundation of its reliability. These tests ensure the system can survive cascading failures without degrading.

**Test Coverage**:
- ✓ Exponential backoff progression
- ✓ Rate limiting with configurable windows
- ✓ Multi-policy independence
- ✓ Realistic watchdog supervision scenarios

**Example**:
```cpp
// Simulates server crashing 5 times - watchdog should handle gracefully
TEST_F(RestartPolicyTest, RealisticWatchdogScenario)
```

### 2. Energy-Aware Scheduling Tests (`test_scheduler_gtest.cpp`)

Tests GridGuard's unique **spot price optimization** logic:

- **Cost Minimization**: Finds cheapest electricity periods for running loads
- **Deadline Constraints**: Respects user requirements (e.g., EV charged by 7 AM)
- **Multi-Hour Optimization**: Handles long-running loads (dishwasher, EV charging)
- **Savings Calculation**: Compares optimized schedule vs. immediate start

**Why This Matters**: This is GridGuard's *raison d'être* - saving users money on electricity by intelligent scheduling.

**Test Coverage**:
- ✓ Single-hour and multi-hour window selection
- ✓ Deadline constraint handling
- ✓ Partial hour calculations (e.g., 30-minute loads)
- ✓ Realistic scenarios: EV charging (8h), washing machine (90min), dishwasher (2h)
- ✓ Savings calculation vs. immediate execution

**Example**:
```cpp
// EV charging: find cheapest 8-hour window before 7 AM deadline
TEST_F(LoadSchedulerTest, ElectricVehicleChargingScenario)
```

### 3. Concurrent Queue Stress Tests (`test_queue_concurrent_gtest.cpp`)

Tests GridGuard's **thread-safe work distribution** under load:

- **Multi-Producer/Multi-Consumer**: Simulates fetcher → parser → compute pipeline
- **Race Condition Detection**: ASAN/UBSAN enabled for memory safety
- **Blocking Behavior**: Verifies producers block when queue full, consumers when empty
- **Graceful Shutdown**: Tests that all threads exit cleanly

**Why This Matters**: GridGuard's multi-process architecture (server, fetcher, parser) relies on reliable work queuing. Data loss or corruption here would be catastrophic.

**Test Coverage**:
- ✓ Multiple producers + single consumer (4 fetchers → 1 parser)
- ✓ Single producer + multiple consumers (1 server → 8 workers)
- ✓ High concurrency stress test (8 producers + 8 consumers)
- ✓ Blocking behavior verification
- ✓ Graceful shutdown with pending threads
- ✓ Data integrity checks (no lost or corrupted items)
- ✓ Realistic GridGuard pipeline simulation

**Example**:
```cpp
// Simulates fetcher → parser pipeline with realistic delays
TEST_F(QueueConcurrentTest, RealisticGridGuardWorkload)
```

### 4. JWT Tests (`test_jwt_gtest.cpp`)

Tests the full token lifecycle: create → validate → reject.

**Test Coverage**:
- ✓ Valid token accepted
- ✓ Expired token rejected
- ✓ Wrong secret rejected
- ✓ Missing `GRIDGUARD_JWT_SECRET` env var
- ✓ Unsupported algorithm (RS256) rejected
- ✓ Malformed/null inputs

### 5. RuntimeConfig Tests (`test_runtime_config_gtest.cpp`)

Tests the three-level fallback chain: config file → env var → default.

**Test Coverage**:
- ✓ Values read from config file
- ✓ Env var override when file is absent
- ✓ Defaults applied when neither source is present
- ✓ Invalid values handled gracefully

### 6. API Endpoint URL Builder Tests (`test_api_endpoints_gtest.cpp`)

Verifies that URL construction produces well-formed strings for both the Elpriset and Open-Meteo APIs.

**Test Coverage**:
- ✓ Correct date formatting (today/tomorrow)
- ✓ Region codes embedded correctly (SE1–SE4)
- ✓ Latitude/longitude in Open-Meteo URL
- ✓ Edge cases: midnight, DST boundaries, year rollover

### 7. Compute Signal Tests (`test_compute_gtest.cpp`)

Tests the BUY/SELL/AVOID/IDLE signal generation under controlled price and solar conditions.

**Design**: All grid fees set to 0 so total cost = (spot + 0.40 tax) × 1.25 VAT — makes thresholds deterministic.

**Test Coverage**:
- ✓ BUY signal at low spot price
- ✓ SELL signal at high spot price with available solar
- ✓ IDLE when price is mid-range
- ✓ Solar-only output (no grid needed)
- ✓ Zero solar edge case

### 8. WorkCompletion Tests (`test_work_completion_gtest.cpp`)

Tests the one-shot signal/wait synchronization primitive used to notify the main thread when async work finishes.

**Test Coverage**:
- ✓ Signal delivered before wait returns
- ✓ Wait blocks until signal is sent
- ✓ Thread-safe signalling from worker thread

### 9. Heartbeat Tests (`test_heartbeat_gtest.cpp`)

Tests the pipe-based health-check primitive used by the watchdog to detect unresponsive child processes.

**Test Coverage**:
- ✓ Beat written successfully
- ✓ Watchdog detects missing beat (timeout)
- ✓ Repeated beats don't overflow pipe buffer
- ✓ Heartbeat stops after pipe close

### 10. ScheduleDB Tests (`test_schedule_db_gtest.cpp`)

Tests SQLite-backed schedule persistence using an in-memory database so no disk state leaks between tests.

**Test Coverage**:
- ✓ Insert and retrieve schedule entries
- ✓ Update existing entry
- ✓ Delete entry
- ✓ Empty result on unknown user

### 12. Process Pipeline Integration Tests (`test_process_pipeline_gtest.cpp`)

Tests the full multi-process IPC boundary at the `fork()`/`exec()` level — the architecture that makes GridGuard distinct from a simple monolith.

**Test Coverage**:
- ✓ Child sends heartbeat beat → parent `Heartbeat_Check` returns success
- ✓ Dead process (closed pipe) detected via `Heartbeat_Check` returning EOF
- ✓ Frozen process (pipe open, no beat) detected via timeout
- ✓ Spawn and wait for three simultaneous processes (server/fetcher/parser)
- ✓ Kill → restart cycle with `RestartPolicy` tracking
- ✓ Watchdog monitors three processes via independent heartbeat pipes

### 13. Chaos Engineering Tests (`test_chaos_watchdog_gtest.cpp`)

Tests worst-case crash scenarios — kernel-level kills that give processes no chance to clean up — and verifies the watchdog responds correctly.

**Test Coverage**:
- ✓ `SIGKILL` cannot be ignored (unlike `SIGTERM`)
- ✓ `SIGKILL` terminates immediately (`waitpid` confirms, `kill(pid, 0)` → `ESRCH`)
- ✓ Heartbeat pipe closed on `SIGKILL` (kernel auto-closes all fds)
- ✓ Five-crash `SIGKILL` storm hits rate limit
- ✓ Exponential backoff progression after repeated `SIGKILL` crashes
- ✓ Simultaneous kill of all three processes (server + fetcher + parser)
- ✓ Full crash → restart → crash cycle with heartbeat validation

### 11. UserConfigDB Tests (`test_user_config_db_gtest.cpp`)

Tests SQLite-backed user config persistence (lat/lon, region, solar area/efficiency) using an in-memory database.

**Test Coverage**:
- ✓ Upsert creates new row
- ✓ Upsert updates existing row
- ✓ Retrieved values match stored values
- ✓ Missing user returns default/null config

## Build System

### CMake Configuration

- **Google Test**: Automatically fetched via FetchContent (v1.14.0)
- **ASAN/UBSAN**: Enabled in Debug builds for memory safety
- **Thread Sanitizer**: Can be enabled for race condition detection
- **Parallel Build**: Supports `-j` for fast compilation

### Sanitizers (Debug Build)

```bash
CMAKE_C_FLAGS_DEBUG   += -fsanitize=address -fsanitize=undefined
CMAKE_CXX_FLAGS_DEBUG += -fsanitize=address -fsanitize=undefined
```

**ASAN** detects:
- Memory leaks
- Use-after-free
- Buffer overflows
- Stack/heap corruption

**UBSAN** detects:
- Integer overflows
- Division by zero
- Null pointer dereferences
- Undefined behavior

## Running Tests

### Quick Start
```bash
make test-gtest          # Run all Google Tests with ASAN/UBSAN
make build-gtest         # Build tests without running
make clean-gtest         # Clean CMake build directory
make test-valgrind       # Run unit tests under Valgrind memcheck
make test-tsan           # Build and run all tests with ThreadSanitizer
make clean-tsan          # Clean TSan build directory
```

### CMake Direct
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

### Individual Test Suites
```bash
./build/tests/test_restart_policy_gtest    # Watchdog resilience
./build/tests/test_scheduler_gtest         # Energy optimization
./build/tests/test_queue_concurrent_gtest  # Concurrency stress
```

### Verbose Output
```bash
cd build && ctest --verbose
./build/tests/test_scheduler_gtest --gtest_filter="*EV*"
```

## Test Metrics

```
Total Tests: 163
├── Logger:              7 tests
├── HTTPRequest:         8 tests
├── HTTPResponse:       13 tests
├── ConfigParser:        5 tests
├── RestartPolicy:      11 tests  ← Watchdog resilience
├── LoadScheduler:      14 tests  ← Energy optimization
├── Queue:              16 tests  ← Concurrency stress
├── JWT:                11 tests  ← Token lifecycle
├── RuntimeConfig:       9 tests  ← Config fallback chain
├── APIEndpoints:       15 tests  ← URL construction
├── Compute:             9 tests  ← BUY/SELL/AVOID/IDLE signals
├── WorkCompletion:      5 tests  ← Sync primitive
├── Heartbeat:           8 tests  ← Watchdog IPC
├── ScheduleDB:          8 tests  ← SQLite persistence
├── UserConfigDB:        7 tests  ← SQLite persistence
├── ProcessPipeline:     8 tests  ← Multi-process IPC integration
└── ChaosWatchdog:       9 tests  ← SIGKILL chaos engineering
```

**Code Coverage**:
- RestartPolicy: 100% (all branches)
- LoadScheduler: 100% (all edge cases)
- Queue: 100% (including shutdown paths)

## Test Philosophy

**Domain-First Testing**: We test *what GridGuard does* (optimize energy costs), not just *how it's built*

**Failure-Oriented**: Most tests simulate failures and verify recovery - crashes, restarts, timeouts

**Realistic Workloads**: EV charging, washing machines, server crashes - not artificial data

**Concurrency Stress**: All shared resources tested under heavy multi-threaded load

## Future Improvements

- [x] Integration tests for full watchdog → server → fetcher → parser pipeline (`test_process_pipeline_gtest.cpp`)
- [x] Chaos engineering: `SIGKILL` processes and verify watchdog recovery (`test_chaos_watchdog_gtest.cpp`)
- [ ] Performance benchmarks with Google Benchmark
- [x] Valgrind integration in Makefile (`make test-valgrind`)
- [x] ThreadSanitizer tests (`make test-tsan`)

## Contributing

When adding tests:

1. **Think Production**: What can go wrong in production? Test that.
2. **Test Failures**: Don't just test success paths - test recovery from failure
3. **Use Realistic Data**: Actual spot prices, real appliance power consumption
4. **Stress Concurrency**: If it's thread-safe, prove it with stress tests
5. **Document Intent**: Explain *why* the test matters in comments

---

**Test Suite Version**: 1.2.0
**Last Updated**: 2026-03-20
**Maintainer**: GridGuard Team
