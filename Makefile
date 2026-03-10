# GR1DGU4RD MAKEFILE 

CC      = gcc
CXX     = g++
CFLAGS  = -Wall -Wextra -Werror -std=c11 -pthread -g -I src
CXXFLAGS = -Wall -Wextra -Werror -std=c++17 -pthread -g -I src
LDFLAGS = -pthread -lmbedtls -lmbedx509 -lmbedcrypto -lsqlite3 -lssl -lcrypto -lrt

# Kontrollera beroenden
ifeq ($(wildcard /usr/include/mbedtls/ssl.h),)
$(error mbedtls-devel saknas. Installera: sudo dnf install mbedtls-devel)
endif
ifeq ($(wildcard /usr/include/sqlite3.h),)
$(error sqlite-devel saknas. Installera: sudo dnf install sqlite-devel)
endif

SRC = src
BIN = bin
BUILD = build

# ── Binärer ────────────────────────────────────────────────────────────
MAIN_BIN     = $(BIN)/GridGuard
SERVER_BIN   = $(BIN)/GridGuard-server
FETCHER_BIN  = $(BIN)/GridGuard-fetcher
PARSER_BIN   = $(BIN)/GridGuard-parser
WATCHDOG_BIN = $(BIN)/GridGuard-watchdog
CLIENT_BIN   = $(BIN)/GridGuard-client

# ── Delade objekt (används av flera binärer) ───────────────────────────
SYS_SRCS = \
    $(SRC)/sys/Logger.c \
    $(SRC)/sys/Queue.c \
    $(SRC)/sys/WorkCompletion.c \
    $(SRC)/sys/CompletionRegistry.c \
    $(SRC)/sys/ThreadPool.c \
    $(SRC)/sys/WorkerPool.c \
    $(SRC)/sys/SignalHandler.c \
    $(SRC)/sys/Daemon.c \
    $(SRC)/sys/PidFile.c

# ── Server ─────────────────────────────────────────────────────────────
SERVER_SRCS = \
    $(SRC)/server/main.c \
    $(SRC)/server/Server.c \
    $(SRC)/server/ClientHandler.c \
    $(SRC)/server/GridGuard.c \
    $(SRC)/compute/Compute.c \
    $(SRC)/compute/ComputeWorker.c \
    $(SRC)/domain/Scheduler.c \
    $(SRC)/watchdog/Metrics.c \
    $(SRC)/sys/SignalHandler.c $(SRC)/sys/Daemon.c $(SRC)/sys/PidFile.c $(SRC)/sys/SignalHandler.c \
    $(SRC)/api/APIEndpoints.c \
    $(SRC)/api/APIParser.c \
    $(SRC)/db/ClientDB.c \
    $(SRC)/db/UserConfigDB.c \
    $(SRC)/db/ScheduleDB.c \
    $(SRC)/cache/SharedCache.c \
    $(SRC)/auth/JWTValidator.c \
    $(SRC)/net/TCPServer.c \
    $(SRC)/net/HTTPRequest.c \
    $(SRC)/net/HTTPResponse.c \
    $(SRC)/net/HTTPClient.c \
    $(SRC)/libs/cJSON.c \
    $(SYS_SRCS)

# ── Fetcher ────────────────────────────────────────────────────────────
FETCHER_SRCS = \
    $(SRC)/fetcher/main.c \
    $(SRC)/fetcher/Fetcher.c \
    $(SRC)/net/HTTPClient.c \
    $(SRC)/net/HTTPFetcher.c \
    $(SRC)/cache/SharedCache.c \
    $(SRC)/api/APIEndpoints.c \
    $(SRC)/sys/Logger.c \
    $(SRC)/sys/ProcessHeartbeat.c \
    $(SRC)/libs/cJSON.c

# ── Parser ─────────────────────────────────────────────────────────────
PARSER_SRCS = \
    $(SRC)/parser/main.c \
    $(SRC)/parser/Parser.c \
    $(SRC)/api/APIParser.c \
    $(SRC)/sys/Logger.c \
    $(SRC)/sys/ProcessHeartbeat.c \
    $(SRC)/libs/cJSON.c

# ── Watchdog ───────────────────────────────────────────────────────────
WATCHDOG_SRCS = \
    $(SRC)/watchdog/main.c \
    $(SRC)/watchdog/Watchdog.c \
    $(SRC)/watchdog/Heartbeat.c \
    $(SRC)/watchdog/RestartPolicy.c \
    $(SRC)/watchdog/Signals.c \
    $(SRC)/watchdog/Metrics.c \
    $(SRC)/watchdog/IPC.c \
    $(SRC)/watchdog/Status.c \
    $(SRC)/watchdog/ProcessSpawner.c \
    $(SRC)/sys/Daemon.c \
    $(SRC)/sys/PidFile.c \
    $(SRC)/sys/Logger.c

# ── Platform-objekt (för generate_jwt.py — länkas EJ in i server) ──────
PLATFORM_SRCS = \
    $(SRC)/auth/JWTIssuer.c \
    $(SRC)/auth/PlatformDB.c

# ── C++-klient ─────────────────────────────────────────────────────────
CLIENT_SRCS = \
    $(SRC)/client/main.cpp \
    $(SRC)/client/HttpClient.cpp \
    $(SRC)/client/GridGuardClient.cpp \
    $(SRC)/client/UserConfigWrapper.cpp

# ── Objektfiler ────────────────────────────────────────────────────────
SERVER_OBJS   = $(SERVER_SRCS:$(SRC)/%.c=$(BUILD)/%.o)
FETCHER_OBJS  = $(FETCHER_SRCS:$(SRC)/%.c=$(BUILD)/%.o)
PARSER_OBJS   = $(PARSER_SRCS:$(SRC)/%.c=$(BUILD)/%.o)
WATCHDOG_OBJS = $(WATCHDOG_SRCS:$(SRC)/%.c=$(BUILD)/%.o)
PLATFORM_OBJS = $(PLATFORM_SRCS:$(SRC)/%.c=$(BUILD)/%.o)
CLIENT_OBJS   = $(CLIENT_SRCS:$(SRC)/%.cpp=$(BUILD)/%.o)

# ── Default target ─────────────────────────────────────────────────────
.PHONY: all
all: directories $(MAIN_BIN) $(SERVER_BIN) $(FETCHER_BIN) $(PARSER_BIN) $(WATCHDOG_BIN) $(CLIENT_BIN) platform-objects

.PHONY: server
server: directories $(SERVER_BIN) $(FETCHER_BIN) $(PARSER_BIN) $(WATCHDOG_BIN)

# ── Kompilera platform-objekt (används av generate_jwt.py) ────────────
.PHONY: platform-objects
platform-objects: directories $(PLATFORM_OBJS)

# ── Skapa kataloger ────────────────────────────────────────────────────
.PHONY: directories
directories:
	@mkdir -p $(BIN) logs
	@mkdir -p $(BUILD)/server $(BUILD)/fetcher $(BUILD)/parser $(BUILD)/watchdog
	@mkdir -p $(BUILD)/compute $(BUILD)/domain $(BUILD)/api $(BUILD)/db
	@mkdir -p $(BUILD)/cache $(BUILD)/auth $(BUILD)/net $(BUILD)/sys
	@mkdir -p $(BUILD)/ipc $(BUILD)/libs $(BUILD)/client
	@mkdir -p $(BUILD)/tests/unit $(BUILD)/tests/integration $(BUILD)/tests/benchmarks

# ── Länkning ───────────────────────────────────────────────────────────
$(SERVER_BIN): $(SERVER_OBJS)
	@echo "Linking server..."
	$(CXX) -o $@ $^ $(LDFLAGS)

$(FETCHER_BIN): $(FETCHER_OBJS)
	@echo "Linking fetcher..."
	$(CC) -o $@ $^ $(LDFLAGS)

$(PARSER_BIN): $(PARSER_OBJS)
	@echo "Linking parser..."
	$(CC) -o $@ $^ $(LDFLAGS)

# ── Main launcher ──────────────────────────────────────────────────────
$(MAIN_BIN): $(SRC)/main.c
	@echo "Building main launcher..."
	$(CC) $(CFLAGS) -o $@ $<

$(WATCHDOG_BIN): $(WATCHDOG_OBJS)
	@echo "Linking watchdog..."
	$(CC) -o $@ $^ $(LDFLAGS)

$(CLIENT_BIN): $(CLIENT_OBJS)
	@echo "Linking client..."
	$(CXX) $(CXXFLAGS) -o $@ $^

# ── Kompilering ────────────────────────────────────────────────────────
$(BUILD)/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ── Individuella targets ────────────────────────────────────────────────
.PHONY: client watchdog
client: directories $(CLIENT_BIN)
	$(CLIENT_BIN)
watchdog: directories $(WATCHDOG_BIN)

# ── Bygg-varianter ─────────────────────────────────────────────────────
.PHONY: debug release profile coverage
debug: CFLAGS += -DDEBUG -O0
debug: CXXFLAGS += -DDEBUG -O0
debug: all

release: CFLAGS += -O2 -DNDEBUG
release: CXXFLAGS += -O2 -DNDEBUG
release: clean all

profile: CFLAGS += -pg -O2
profile: CXXFLAGS += -pg -O2
profile: LDFLAGS += -pg
profile: clean all

coverage: CFLAGS += --coverage -O0
coverage: CXXFLAGS += --coverage -O0
coverage: LDFLAGS += --coverage
coverage: clean all

# ── Benchmarks ─────────────────────────────────────────────────────────
BENCH_COMPUTE_BIN = $(BIN)/bench_compute
BENCH_QUEUE_BIN   = $(BIN)/bench_queue
BENCH_CACHE_BIN   = $(BIN)/bench_cache

BENCH_COMPUTE_SRCS = \
    $(SRC)/tests/benchmarks/bench_compute.c \
    $(SRC)/compute/Compute.c \
    $(SRC)/sys/Logger.c

BENCH_QUEUE_SRCS = \
    $(SRC)/tests/benchmarks/bench_queue.c \
    $(SRC)/sys/Queue.c \
    $(SRC)/sys/Logger.c

BENCH_CACHE_SRCS = \
    $(SRC)/tests/benchmarks/bench_cache.c \
    $(SRC)/cache/SharedCache.c \
    $(SRC)/sys/Logger.c

$(BENCH_COMPUTE_BIN): directories $(BENCH_COMPUTE_SRCS)
	$(CC) $(CFLAGS) -O2 -D_GNU_SOURCE -o $@ $(BENCH_COMPUTE_SRCS) $(LDFLAGS) -lm

$(BENCH_QUEUE_BIN): directories $(BENCH_QUEUE_SRCS)
	$(CC) $(CFLAGS) -O2 -o $@ $(BENCH_QUEUE_SRCS) $(LDFLAGS)

$(BENCH_CACHE_BIN): directories $(BENCH_CACHE_SRCS)
	$(CC) $(CFLAGS) -O2 -o $@ $(BENCH_CACHE_SRCS) $(LDFLAGS)

.PHONY: bench-compute bench-queue bench-cache bench
bench-compute: $(BENCH_COMPUTE_BIN)
	@echo ""
	@echo "Running bench_compute..."
	@echo ""
	@$(BENCH_COMPUTE_BIN)

bench-queue: $(BENCH_QUEUE_BIN)
	@echo ""
	@echo "Running bench_queue..."
	@echo ""
	@$(BENCH_QUEUE_BIN)

bench-cache: $(BENCH_CACHE_BIN)
	@echo ""
	@echo "Running bench_cache..."
	@echo ""
	@$(BENCH_CACHE_BIN)

bench: bench-compute bench-queue bench-cache
	@echo ""
	@echo "All benchmarks completed."

.PHONY: valgrind-bench cachegrind
BENCH_COMPUTE_VALGRIND_BIN = $(BIN)/bench_compute_valgrind

$(BENCH_COMPUTE_VALGRIND_BIN): directories $(BENCH_COMPUTE_SRCS)
	$(CC) $(CFLAGS) -g -D_GNU_SOURCE -DVALGRIND_RUN -o $@ $(BENCH_COMPUTE_SRCS) $(LDFLAGS) -lm

valgrind-bench: $(BENCH_COMPUTE_VALGRIND_BIN)
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
	         --error-exitcode=1 $(BENCH_COMPUTE_VALGRIND_BIN)

cachegrind: $(BENCH_COMPUTE_BIN)
	valgrind --tool=cachegrind --cachegrind-out-file=cachegrind.out \
	         $(BENCH_COMPUTE_BIN)
	cg_annotate cachegrind.out

# ── Tester ─────────────────────────────────────────────────────────────
TEST_COMMON = $(SRC)/sys/Logger.c $(SRC)/libs/cJSON.c

TEST_JWT_BIN      = $(BIN)/test_jwt_validator
TEST_HTTP_REQ_BIN = $(BIN)/test_http_request
TEST_HTTP_RESP_BIN = $(BIN)/test_http_response
TEST_LOGGER_BIN   = $(BIN)/test_logger
TEST_API_BIN      = $(BIN)/test_api_fetch
TEST_WEATHER_BIN  = $(BIN)/test_openmeteo_parser
TEST_PIPELINE_BIN = $(BIN)/test_pipeline

$(TEST_JWT_BIN): $(SRC)/tests/unit/test_jwt_validator.c $(SRC)/auth/JWTValidator.c $(SRC)/sys/Logger.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_HTTP_REQ_BIN): $(SRC)/tests/unit/test_http_request.c $(SRC)/net/HTTPRequest.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_HTTP_RESP_BIN): $(SRC)/tests/unit/test_http_response.c $(SRC)/net/HTTPResponse.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_LOGGER_BIN): $(SRC)/tests/unit/test_logger.c $(SRC)/sys/Logger.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_API_BIN): $(SRC)/tests/integration/test_api_fetch.c \
    $(SRC)/api/APIEndpoints.c $(SRC)/api/APIParser.c \
    $(SRC)/net/HTTPClient.c $(SRC)/net/HTTPFetcher.c \
    $(SRC)/sys/Logger.c $(SRC)/libs/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_WEATHER_BIN): $(SRC)/tests/integration/test_openmeteo_parser.c \
    $(SRC)/api/APIEndpoints.c $(SRC)/api/APIParser.c \
    $(SRC)/net/HTTPClient.c $(SRC)/net/HTTPFetcher.c \
    $(SRC)/sys/Logger.c $(SRC)/libs/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_PIPELINE_BIN): $(SRC)/tests/integration/test_pipeline.c \
    $(SRC)/server/ClientHandler.c $(SRC)/server/Server.c \
    $(SRC)/net/TCPServer.c $(SRC)/net/HTTPRequest.c $(SRC)/net/HTTPResponse.c \
    $(SRC)/auth/JWTValidator.c $(SRC)/db/ScheduleDB.c $(SRC)/domain/Scheduler.c \
    $(SRC)/sys/SignalHandler.c $(SRC)/sys/Daemon.c $(SRC)/sys/PidFile.c $(SRC)/sys/SignalHandler.c \
    $(SRC)/server/GridGuard.c \
    $(SRC)/compute/Compute.c $(SRC)/compute/ComputeWorker.c \
    $(SRC)/api/APIEndpoints.c $(SRC)/api/APIParser.c \
    $(SRC)/cache/SharedCache.c \
    $(SRC)/net/HTTPClient.c $(SRC)/net/HTTPFetcher.c \
    $(SRC)/db/ClientDB.c $(SRC)/db/UserConfigDB.c $(SRC)/db/ScheduleDB.c \
    $(SRC)/sys/Queue.c $(SRC)/sys/WorkCompletion.c $(SRC)/sys/CompletionRegistry.c \
    $(SRC)/sys/ThreadPool.c $(SRC)/sys/WorkerPool.c \
    $(SRC)/sys/Logger.c $(SRC)/libs/cJSON.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: test test-jwt test-http-request test-http-response test-logger test-api test-weather test-pipeline
test: test-jwt test-http-request test-http-response test-logger test-api test-weather test-pipeline
	@echo "======================================"
	@echo "All tests passed!"
	@echo "======================================"

test-jwt: directories $(TEST_JWT_BIN)
	@GRIDGUARD_JWT_SECRET=gridguard-test-secret $(TEST_JWT_BIN)

test-http-request: directories $(TEST_HTTP_REQ_BIN)
	@$(TEST_HTTP_REQ_BIN)

test-http-response: directories $(TEST_HTTP_RESP_BIN)
	@$(TEST_HTTP_RESP_BIN)

test-logger: directories $(TEST_LOGGER_BIN)
	@$(TEST_LOGGER_BIN)

test-api: directories $(TEST_API_BIN)
	@$(TEST_API_BIN)

test-weather: directories $(TEST_WEATHER_BIN)
	@$(TEST_WEATHER_BIN)

test-pipeline: directories $(TEST_PIPELINE_BIN)
	@$(TEST_PIPELINE_BIN)

# ── Körning ────────────────────────────────────────────────────────────
.PHONY: run-server server-run run-watchdog run-client start dev stop

# Start server with watchdog supervision (recommended)
run-server: server
	@echo "Starting GridGuard with watchdog supervision..."
	@SECRET=$${GRIDGUARD_JWT_SECRET:-gridguard-test-secret}; \
	setsid env GRIDGUARD_JWT_SECRET="$$SECRET" GRIDGUARD_DB_PATH="$(CURDIR)/gridguard.db" $(WATCHDOG_BIN) >/dev/null 2>&1 & \
	echo $$! > /tmp/gridguard.pid; \
	echo "Watchdog started (PID $$!)"; \
	echo "Logs: logs/watchdog.log, logs/server.log, logs/fetcher.log, logs/parser.log"; \
	echo "Stop with: make stop"

# Alias for run-server
server-run: run-server

# Start watchdog (requires GRIDGUARD_JWT_SECRET)
run-watchdog: server
	@if [ -z "$$GRIDGUARD_JWT_SECRET" ]; then \
	    echo "Error: GRIDGUARD_JWT_SECRET not set."; exit 1; fi
	@setsid env GRIDGUARD_JWT_SECRET="$$GRIDGUARD_JWT_SECRET" GRIDGUARD_DB_PATH="$(CURDIR)/gridguard.db" $(WATCHDOG_BIN) >/dev/null 2>&1 & \
	echo $$! > /tmp/gridguard.pid
	@echo "Watchdog started. Logs: logs/watchdog.log · logs/server.log"

run-client: client
	$(CLIENT_BIN) $(ARGS)

# Quick start: build and run (no DB seeding)
start: server
	@echo "=== GridGuard Start ==="
	@if [ -f /tmp/gridguard.pid ]; then \
	    echo "Stopping existing instance..."; \
	    $(MAKE) stop 2>/dev/null || true; fi
	@pkill -9 GridGuard 2>/dev/null || true
	@fuser -k -9 8080/tcp 2>/dev/null || true
	@rm -f /tmp/gridguard* 2>/dev/null || true
	@sleep 0.5
	@SECRET=$${GRIDGUARD_JWT_SECRET:-gridguard-test-secret}; \
	echo "[1/2] Starting GridGuard..."; \
	setsid env GRIDGUARD_JWT_SECRET="$$SECRET" GRIDGUARD_DB_PATH="$(CURDIR)/gridguard.db" $(MAIN_BIN) >/dev/null 2>&1 & \
	sleep 0.2; \
	WATCHDOG_PID=$$(cat /tmp/gridguard.pid 2>/dev/null); \
	echo "      GridGuard PID: $$WATCHDOG_PID"; \
	printf "      Waiting for server"; \
	for i in $$(seq 1 20); do \
	    curl -sf http://localhost:8080/health >/dev/null 2>&1 && break; \
	    printf "."; sleep 0.5; done; echo " ready"; \
	echo "[2/2] System running"; \
	echo "      API:  http://localhost:8080"; \
	echo "      Stop: make stop"; \
	echo "      Logs: logs/*.log"; \
	echo ""; \
	echo "NOTE: Databases are NOT seeded automatically."; \
	echo "      Seed manually: python3 scripts/seed_platform.py platform.db"; \
	echo "                     python3 scripts/seed_client.py gridguard.db"

dev: server watchdog platform-objects
	@if [ -f /tmp/gridguard.pid ]; then \
	    kill -9 $$(cat /tmp/gridguard.pid) 2>/dev/null; rm -f /tmp/gridguard.pid; fi
	@pkill -9 GridGuard 2>/dev/null || true
	@fuser -k -9 8080/tcp 2>/dev/null || true
	@rm -f /tmp/gridguard* 2>/dev/null || true
	@sleep 0.5
	@echo "GridGuard Development Environment"
	@SECRET=$${GRIDGUARD_JWT_SECRET:-demo_secret_key_change_in_production_2024}; \
	echo "[1/5] Platform registration"; \
	rm -f "$(CURDIR)/platform.db" "$(CURDIR)/gridguard.db"; \
	python3 scripts/seed_platform.py "$(CURDIR)/platform.db"; \
	echo "[2/5] JWT token issuance"; \
	DEV_TOKEN=$$(GRIDGUARD_JWT_SECRET="$$SECRET" python3 scripts/generate_jwt.py "$(CURDIR)/platform.db" test_user 2>/dev/null); \
	if [ -z "$$DEV_TOKEN" ]; then echo "ERROR: Token generation failed"; exit 1; fi; \
	echo "      Token: $$(echo $$DEV_TOKEN | cut -c1-50)..."; \
	echo "[3/5] Client device setup"; \
	python3 scripts/seed_client.py "$(CURDIR)/gridguard.db"; \
	echo "[4/5] Starting GridGuard"; \
	setsid env GRIDGUARD_JWT_SECRET="$$SECRET" GRIDGUARD_DB_PATH="$(CURDIR)/gridguard.db" $(MAIN_BIN) >/dev/null 2>&1 & \
	MAIN_PID=$$!; echo $$MAIN_PID > /tmp/gridguard.pid; \
	echo "      GridGuard PID: $$MAIN_PID"; \
	printf "Waiting for server"; \
	for i in $$(seq 1 20); do \
	    curl -sf http://localhost:8080/health >/dev/null 2>&1 && break; \
	    printf "."; sleep 0.5; done; echo " ready"; \
	SERVER_PID=$$(cat /tmp/gridguard.pid 2>/dev/null); \
	[ -n "$$SERVER_PID" ] && echo "      Server  PID: $$SERVER_PID"; \
	FETCHER_PID=$$(pgrep -f GridGuard-fetcher 2>/dev/null | head -1); \
	[ -n "$$FETCHER_PID" ] && echo "      Fetcher PID: $$FETCHER_PID"; \
	PARSER_PID=$$(pgrep -f GridGuard-parser 2>/dev/null | head -1); \
	[ -n "$$PARSER_PID" ] && echo "      Parser  PID: $$PARSER_PID"; \
	echo "[5/5] Testing authenticated requests"; \
	echo "GET /forecast"; \
	curl -s -X GET "http://localhost:8080/forecast" \
	    -H "Authorization: Bearer $$DEV_TOKEN" | python3 -m json.tool 2>/dev/null || echo "Request failed"; \
	echo "GET /schedule"; \
	curl -s -X GET "http://localhost:8080/schedule" \
	    -H "Authorization: Bearer $$DEV_TOKEN" | python3 -m json.tool 2>/dev/null || echo "Request failed";

stop:
	@echo "Stopping GridGuard gracefully..."
	@if [ -f /tmp/gridguard.pid ]; then \
	    PID=$$(cat /tmp/gridguard.pid 2>/dev/null); \
	    if [ -n "$$PID" ]; then \
	        echo "  [→] Sending SIGTERM to watchdog PID $$PID"; \
	        if kill -TERM $$PID 2>/dev/null; then \
	            echo "  [→] Waiting for graceful shutdown..."; \
	            for i in 1 2 3 4 5; do \
	                if ! kill -0 $$PID 2>/dev/null; then \
	                    echo "  [OK] Watchdog stopped gracefully"; \
	                    rm -f /tmp/gridguard.pid; \
	                    exit 0; \
	                fi; \
	                sleep 1; \
	            done; \
	            echo "  [!!] Watchdog did not stop, forcing..."; \
	            kill -9 $$PID 2>/dev/null; \
	            echo "  [OK] Watchdog killed (SIGKILL)"; \
	        fi; \
	        rm -f /tmp/gridguard.pid; \
	    fi; \
	else \
	    echo "  [i] No PID file found"; \
	fi
	@echo "  [→] Cleaning up stray processes (if any)..."
	@sleep 1
	@FETCHER_PID=$$(pgrep -f GridGuard-fetcher 2>/dev/null | head -1); \
	if [ -n "$$FETCHER_PID" ]; then \
	    echo "  [!!] Stray fetcher PID $$FETCHER_PID found, killing..."; \
	    kill -9 $$FETCHER_PID 2>/dev/null; \
	fi
	@PARSER_PID=$$(pgrep -f GridGuard-parser 2>/dev/null | head -1); \
	if [ -n "$$PARSER_PID" ]; then \
	    echo "  [!!] Stray parser PID $$PARSER_PID found, killing..."; \
	    kill -9 $$PARSER_PID 2>/dev/null; \
	fi
	@SERVER_PID=$$(pgrep -f GridGuard-server 2>/dev/null | head -1); \
	if [ -n "$$SERVER_PID" ]; then \
	    echo "  [!!] Stray server PID $$SERVER_PID found, killing..."; \
	    kill -9 $$SERVER_PID 2>/dev/null; \
	fi
	@fuser -k -9 8080/tcp 2>/dev/null && echo "  [→] Port 8080 freed" || true
	@rm -f /tmp/gridguard_*.fifo /tmp/gridguard_*.sock /tmp/gridguard.status 2>/dev/null || true
	@echo "  [✓] GridGuard stopped"

# ── Analys ─────────────────────────────────────────────────────────────
.PHONY: valgrind-server helgrind gprof-analyze
valgrind-server: debug
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes $(SERVER_BIN)

helgrind: debug
	valgrind --tool=helgrind $(SERVER_BIN)

gprof-analyze:
	@if [ -f gmon.out ]; then \
	    gprof $(SERVER_BIN) gmon.out > profile_report.txt; \
	    echo "Profile report: profile_report.txt"; \
	else echo "Kör med 'make profile' först"; fi

# ── IPC-cleanup ────────────────────────────────────────────────────────
.PHONY: clean-ipc
clean-ipc:
	@rm -f /tmp/gridguard_*.fifo /tmp/gridguard*.sock
	@rm -f /dev/shm/gridguard_* /dev/shm/sem.gridguard_*
	@echo "IPC resources cleaned"

# ── Rensning ───────────────────────────────────────────────────────────
.PHONY: clean distclean
clean:
	rm -rf $(BUILD) $(BIN) gmon.out *.gcda *.gcno *.gcov profile_report.txt vgcore.*

distclean: clean
	rm -f logs/*.log *.sock *.pid

# ── Hjälp ──────────────────────────────────────────────────────────────
.PHONY: help
help:
	@echo "GridGuard — tillgängliga targets:"
	@echo ""
	@echo "  all              Bygg allt (server, fetcher, parser, client, platform-objects)"
	@echo "  server           Bygg server + fetcher + parser"
	@echo "  client           Bygg C++-klienten"
	@echo "  watchdog         Bygg watchdog"
	@echo "  platform-objects Kompilera JWTIssuer+PlatformDB .o (för generate_jwt.py)"
	@echo ""
	@echo "  debug / release / profile / coverage"
	@echo ""
	@echo "  test             Kör alla tester"
	@echo "  test-jwt / test-http-request / test-http-response"
	@echo "  test-logger / test-api / test-weather / test-pipeline"
	@echo ""
	@echo "  start            Snabbstart med watchdog (rekommenderad)"
	@echo "  run-server       Starta server med watchdog (samma som start)"
	@echo "  server-run       Alias för run-server"
	@echo "  dev              Utvecklingsmiljö med watchdog + test requests"
	@echo "  stop             Stoppa alla GridGuard-processer"
	@echo "  run-watchdog     Starta watchdog (kräver GRIDGUARD_JWT_SECRET)"
	@echo ""
	@echo "  valgrind-server  Minneskontroll"
	@echo "  helgrind         Trådkontroll"
	@echo "  gprof-analyze    Analysera profileringsdata"
	@echo "  clean-ipc        Rensa IPC-resurser"
	@echo ""
	@echo "  clean / distclean"

.PHONY: all server client watchdog platform-objects directories
.PHONY: debug release profile coverage
.PHONY: test test-jwt test-http-request test-http-response test-logger test-api test-weather test-pipeline
.PHONY: start run-server server-run run-watchdog run-client dev stop
.PHONY: valgrind-server helgrind gprof-analyze clean-ipc clean distclean help
