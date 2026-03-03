# GridGuard Makefile
# Systemprogrammering och introduktion till C++

# Compiler och flaggor
CC = gcc
CXX = g++
LDFLAGS = -pthread -lmbedtls -lmbedx509 -lmbedcrypto -lsqlite3 -lrt

# Kontrollera att mbedtls-devel är installerat
ifeq ($(wildcard /usr/include/mbedtls/ssl.h),)
$(info )
$(info Fel: mbedtls-devel saknas.)
$(info Installera med:)
$(info   Fedora/RHEL:  sudo dnf install mbedtls-devel)
$(info   Ubuntu/Debian: sudo apt install libmbedtls-dev)
$(info )
$(error Avbryter bygget — mbedtls-devel måste installeras först)
endif

# Kontrollera att sqlite-devel är installerat
ifeq ($(wildcard /usr/include/sqlite3.h),)
$(info )
$(info Fel: sqlite-devel saknas.)
$(info Installera med:)
$(info   Fedora/RHEL:  sudo dnf install sqlite-devel)
$(info   Ubuntu/Debian: sudo apt install libsqlite3-dev)
$(info )
$(error Avbryter bygget — sqlite-devel måste installeras först)
endif

# Directories
SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin
TEST_DIR = $(SRC_DIR)/tests
LIBS_DIR = $(SRC_DIR)/libs

# Application directories (domain-specific code)
APPLICATION_DIR = $(SRC_DIR)/application
APP_CORE_DIR = $(APPLICATION_DIR)/core
APP_WORKERS_DIR = $(APPLICATION_DIR)/workers
APP_MODELS_DIR = $(APPLICATION_DIR)/models
APP_MODELS_APIS_DIR = $(APP_MODELS_DIR)/apis
APP_MODELS_DOMAIN_DIR = $(APP_MODELS_DIR)/domain
APP_MODELS_CONFIG_DIR = $(APP_MODELS_DIR)/config
APP_SERVICES_DIR = $(APPLICATION_DIR)/services
APP_API_DIR = $(APPLICATION_DIR)/api
APP_CONFIGS_DIR = $(APPLICATION_DIR)/configs

# Infrastructure directories (platform & runtime)
INFRASTRUCTURE_DIR = $(SRC_DIR)/infrastructure
LOGGING_DIR = $(INFRASTRUCTURE_DIR)/logging
SIGNALS_DIR = $(INFRASTRUCTURE_DIR)/signals
DAEMON_DIR = $(INFRASTRUCTURE_DIR)/daemon
AUTH_DIR = $(INFRASTRUCTURE_DIR)/auth
DATABASE_DIR = $(INFRASTRUCTURE_DIR)/database
CACHE_DIR    = $(INFRASTRUCTURE_DIR)/cache
PROCESSES_DIR = $(INFRASTRUCTURE_DIR)/processes
WATCHDOG_DIR = $(PROCESSES_DIR)/watchdog
FETCHER_DIR = $(PROCESSES_DIR)/fetcher
PARSER_DIR = $(PROCESSES_DIR)/parser

# Network directories
NETWORK_DIR = $(SRC_DIR)/network
NETWORK_TCP_DIR = $(NETWORK_DIR)/tcp
NETWORK_CLIENT_DIR = $(NETWORK_DIR)/client
NETWORK_HTTP_DIR = $(NETWORK_DIR)/http

# Concurrency directories (OS primitives)
CONCURRENCY_DIR = $(SRC_DIR)/concurrency
THREADS_DIR = $(CONCURRENCY_DIR)/threads
SYNC_DIR = $(CONCURRENCY_DIR)/sync

# Include paths for headers
INCLUDES = -I$(SRC_DIR) \
           -I$(LIBS_DIR) \
           -I$(APPLICATION_DIR) \
           -I$(APP_CORE_DIR) \
           -I$(APP_WORKERS_DIR) \
           -I$(FETCHER_DIR) \
           -I$(PARSER_DIR) \
           -I$(APP_MODELS_DIR) \
           -I$(APP_MODELS_APIS_DIR) \
           -I$(APP_MODELS_DOMAIN_DIR) \
           -I$(APP_MODELS_CONFIG_DIR) \
           -I$(APP_SERVICES_DIR) \
           -I$(APP_API_DIR) \
           -I$(APP_CONFIGS_DIR) \
           -I$(INFRASTRUCTURE_DIR) \
           -I$(LOGGING_DIR) \
           -I$(SIGNALS_DIR) \
           -I$(DAEMON_DIR) \
           -I$(WATCHDOG_DIR) \
           -I$(AUTH_DIR) \
           -I$(DATABASE_DIR) \
           -I$(CACHE_DIR) \
           -I$(NETWORK_DIR) \
           -I$(NETWORK_TCP_DIR) \
           -I$(NETWORK_CLIENT_DIR) \
           -I$(NETWORK_HTTP_DIR) \
           -I$(CONCURRENCY_DIR) \
           -I$(THREADS_DIR) \
           -I$(SYNC_DIR)

# Compiler flags
CFLAGS = -Wall -Wextra -Werror -std=c11 -pthread -g $(INCLUDES)
CXXFLAGS = -Wall -Wextra -Werror -std=c++17 -pthread -g $(INCLUDES)

# Output binaries
SERVER_BIN = $(BIN_DIR)/GridGuard-server
FETCHER_BIN = $(BIN_DIR)/GridGuard-fetcher
PARSER_BIN = $(BIN_DIR)/GridGuard-parser
WATCHDOG_BIN = $(BIN_DIR)/GridGuard-watchdog

# Source files for server (main process with HTTP + Compute thread)
SERVER_SRCS_C = $(SRC_DIR)/main.c \
                $(APP_CORE_DIR)/Server.c \
                $(APP_CORE_DIR)/ClientHandler.c \
                $(APP_CORE_DIR)/GridGuard.c \
                $(APP_WORKERS_DIR)/ComputeWorkerHybrid.c \
                $(wildcard $(LOGGING_DIR)/*.c) \
                $(wildcard $(SIGNALS_DIR)/*.c) \
                $(wildcard $(DAEMON_DIR)/*.c) \
                $(wildcard $(AUTH_DIR)/*.c) \
                $(wildcard $(DATABASE_DIR)/*.c) \
                $(wildcard $(CACHE_DIR)/*.c) \
                $(wildcard $(NETWORK_TCP_DIR)/*.c) \
                $(wildcard $(NETWORK_HTTP_DIR)/*.c) \
                $(NETWORK_CLIENT_DIR)/HTTPClient.c \
                $(wildcard $(APP_API_DIR)/*.c) \
                $(wildcard $(APP_MODELS_DOMAIN_DIR)/*.c) \
                $(wildcard $(APP_SERVICES_DIR)/*.c) \
                $(wildcard $(THREADS_DIR)/*.c) \
                $(wildcard $(SYNC_DIR)/*.c) \
                $(wildcard $(LIBS_DIR)/*.c)

# Source files for Fetch process
FETCHER_SRCS = $(FETCHER_DIR)/main.c \
               $(FETCHER_DIR)/fetcher.c \
               $(NETWORK_CLIENT_DIR)/HTTPFetcher.c \
               $(NETWORK_CLIENT_DIR)/HTTPClient.c \
               $(CACHE_DIR)/SharedCache.c \
               $(APP_API_DIR)/APIEndpoints.c \
               $(LOGGING_DIR)/Logger.c \
               $(LIBS_DIR)/cJSON.c

# Source files for Parse process
PARSER_SRCS = $(PARSER_DIR)/main.c \
              $(PARSER_DIR)/parser.c \
              $(APP_API_DIR)/APIParser.c \
              $(LOGGING_DIR)/Logger.c \
              $(LIBS_DIR)/cJSON.c

TEST_SRCS = $(wildcard $(TEST_DIR)/unit/*.c) \
            $(wildcard $(TEST_DIR)/integration/*.c)

# Object files
SERVER_OBJS = $(SERVER_SRCS_C:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
FETCHER_OBJS = $(FETCHER_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
PARSER_OBJS = $(PARSER_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
TEST_OBJS = $(TEST_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Test binary
TEST_BIN = $(BIN_DIR)/test_runner

# Watchdog source files
WATCHDOG_SRCS = $(WATCHDOG_DIR)/main.c \
                $(WATCHDOG_DIR)/Watchdog.c \
                $(WATCHDOG_DIR)/WatchdogSignals.c \
                $(WATCHDOG_DIR)/Heartbeat.c \
                $(WATCHDOG_DIR)/RestartPolicy.c \
                $(DAEMON_DIR)/PidFile.c \
                $(LOGGING_DIR)/Logger.c

WATCHDOG_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(WATCHDOG_SRCS))

# Default target - Build process-based IPC architecture
.PHONY: all
all: directories $(SERVER_BIN) $(FETCHER_BIN) $(PARSER_BIN)

# Individual targets for CI
.PHONY: server
server: directories $(SERVER_BIN) $(FETCHER_BIN) $(PARSER_BIN)

# Create necessary directories
.PHONY: directories
directories:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/application/core
	@mkdir -p $(BUILD_DIR)/application/workers
	@mkdir -p $(BUILD_DIR)/infrastructure/processes/fetcher
	@mkdir -p $(BUILD_DIR)/infrastructure/processes/parser
	@mkdir -p $(BUILD_DIR)/infrastructure/processes/watchdog
	@mkdir -p $(BUILD_DIR)/application/models/apis
	@mkdir -p $(BUILD_DIR)/application/models/domain
	@mkdir -p $(BUILD_DIR)/application/models/config
	@mkdir -p $(BUILD_DIR)/application/services
	@mkdir -p $(BUILD_DIR)/application/api
	@mkdir -p $(BUILD_DIR)/application/configs
	@mkdir -p $(BUILD_DIR)/infrastructure/logging
	@mkdir -p $(BUILD_DIR)/infrastructure/signals
	@mkdir -p $(BUILD_DIR)/infrastructure/daemon
	@mkdir -p $(BUILD_DIR)/infrastructure/auth
	@mkdir -p $(BUILD_DIR)/infrastructure/database
	@mkdir -p $(BUILD_DIR)/infrastructure/cache
	@mkdir -p $(BUILD_DIR)/network/tcp
	@mkdir -p $(BUILD_DIR)/network/client
	@mkdir -p $(BUILD_DIR)/network/http
	@mkdir -p $(BUILD_DIR)/concurrency/threads
	@mkdir -p $(BUILD_DIR)/concurrency/sync
	@mkdir -p $(BUILD_DIR)/concurrency/ipc
	@mkdir -p $(BUILD_DIR)/libs
	@mkdir -p $(BUILD_DIR)/tests/unit
	@mkdir -p $(BUILD_DIR)/tests/integration
	@mkdir -p $(BIN_DIR)
	@mkdir -p logs

# Build server (use g++ because we have C++ files now)
$(SERVER_BIN): $(SERVER_OBJS)
	@echo "Linking server..."
	$(CXX) -o $@ $^ $(LDFLAGS)
	@echo "Server built successfully: $@"

# Build fetcher process
$(FETCHER_BIN): $(FETCHER_OBJS)
	@echo "Linking fetcher process..."
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "Fetcher process built: $@"

# Build parser process
$(PARSER_BIN): $(PARSER_OBJS)
	@echo "Linking parser process..."
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "Parser process built: $@"

# Compile C source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Compile C++ source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Build watchdog
.PHONY: watchdog
watchdog: directories $(WATCHDOG_BIN)

$(WATCHDOG_BIN): $(WATCHDOG_OBJS)
	@echo "Linking watchdog..."
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "Watchdog built successfully: $@"

# Debug build with additional flags
.PHONY: debug
debug: CFLAGS += -DDEBUG -O0
debug: CXXFLAGS += -DDEBUG -O0
debug: all

# Release build with optimizations
.PHONY: release
release: CFLAGS += -O2 -DNDEBUG
release: CXXFLAGS += -O2 -DNDEBUG
release: clean all

# Profiling build
.PHONY: profile
profile: CFLAGS += -pg -O2
profile: CXXFLAGS += -pg -O2
profile: LDFLAGS += -pg
profile: clean all

# Coverage build
.PHONY: coverage
coverage: CFLAGS += --coverage -O0
coverage: CXXFLAGS += --coverage -O0
coverage: LDFLAGS += --coverage
coverage: clean all

# Build test binary
$(TEST_BIN): $(TEST_OBJS)
	@echo "Linking test runner..."
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Test runner built successfully: $@"

# Run all tests
.PHONY: test
test: test-api test-logger test-pipeline test-weather test-jwt test-http-request test-http-response
	@echo ""
	@echo "======================================"
	@echo "All tests passed!"
	@echo "======================================"

# Test binaries
TEST_API_BIN = $(BIN_DIR)/test_api_fetch
TEST_LOGGER_BIN = $(BIN_DIR)/test_logger
TEST_PIPELINE_BIN = $(BIN_DIR)/test_pipeline
TEST_WEATHER_BIN = $(BIN_DIR)/test_openmeteo_parser
TEST_JWT_BIN = $(BIN_DIR)/test_jwt_validator
TEST_HTTP_REQ_BIN = $(BIN_DIR)/test_http_request
TEST_HTTP_RESP_BIN = $(BIN_DIR)/test_http_response

# Test dependencies
TEST_API_DEPS = $(wildcard $(APP_API_DIR)/*.c) \
                $(wildcard $(LOGGING_DIR)/*.c) \
                $(wildcard $(APP_SERVICES_DIR)/*.c) \
                $(wildcard $(NETWORK_CLIENT_DIR)/*.c) \
                $(wildcard $(APP_MODELS_DOMAIN_DIR)/*.c) \
                $(wildcard $(LIBS_DIR)/*.c)

TEST_LOGGER_DEPS = $(LOGGING_DIR)/Logger.c

TEST_PIPELINE_DEPS = $(wildcard $(APP_CORE_DIR)/*.c) \
                     $(wildcard $(APP_WORKERS_DIR)/*.c) \
                     $(wildcard $(APP_SERVICES_DIR)/*.c) \
                     $(wildcard $(CACHE_DIR)/*.c) \
                     $(wildcard $(NETWORK_CLIENT_DIR)/*.c) \
                     $(wildcard $(SYNC_DIR)/*.c) \
                     $(wildcard $(APP_API_DIR)/*.c) \
                     $(wildcard $(LOGGING_DIR)/*.c) \
                     $(wildcard $(DATABASE_DIR)/*.c) \
                     $(wildcard $(APP_MODELS_DOMAIN_DIR)/*.c) \
                     $(wildcard $(LIBS_DIR)/*.c)

TEST_WEATHER_DEPS = $(wildcard $(APP_API_DIR)/*.c) \
                    $(wildcard $(LOGGING_DIR)/*.c) \
                    $(wildcard $(APP_SERVICES_DIR)/*.c) \
                    $(wildcard $(NETWORK_CLIENT_DIR)/*.c) \
                    $(wildcard $(APP_MODELS_DOMAIN_DIR)/*.c) \
                    $(wildcard $(LIBS_DIR)/*.c)

TEST_JWT_DEPS = $(AUTH_DIR)/JWTValidator.c \
                $(LOGGING_DIR)/Logger.c

TEST_HTTP_REQ_DEPS = $(NETWORK_HTTP_DIR)/HTTPRequest.c

TEST_HTTP_RESP_DEPS = $(NETWORK_HTTP_DIR)/HTTPResponse.c

# Build API test
$(TEST_API_BIN): $(TEST_DIR)/integration/test_api_fetch.c $(TEST_API_DEPS)
	@echo "Building API test..."
	$(CC) $(CFLAGS) -o $@ $(TEST_DIR)/integration/test_api_fetch.c $(TEST_API_DEPS) $(LDFLAGS)
	@echo "API test built: $@"

# Build Logger test
$(TEST_LOGGER_BIN): $(TEST_DIR)/unit/test_logger.c $(TEST_LOGGER_DEPS)
	@echo "Building Logger test..."
	$(CC) $(CFLAGS) -o $@ $(TEST_DIR)/unit/test_logger.c $(TEST_LOGGER_DEPS) $(LDFLAGS)
	@echo "Logger test built: $@"

# Build Pipeline test
$(TEST_PIPELINE_BIN): $(TEST_DIR)/integration/test_pipeline.c $(TEST_PIPELINE_DEPS)
	@echo "Building Pipeline test..."
	$(CC) $(CFLAGS) -o $@ $(TEST_DIR)/integration/test_pipeline.c $(TEST_PIPELINE_DEPS) $(LDFLAGS)
	@echo "Pipeline test built: $@"

# Run API test
.PHONY: test-api
test-api: directories $(TEST_API_BIN)
	@echo "Running API fetch test..."
	@$(TEST_API_BIN)

# Run Logger test
.PHONY: test-logger
test-logger: directories $(TEST_LOGGER_BIN)
	@echo "Running Logger test..."
	@$(TEST_LOGGER_BIN)

# Run Pipeline test
.PHONY: test-pipeline
test-pipeline: directories $(TEST_PIPELINE_BIN)
	@echo "Running Pipeline test..."
	@$(TEST_PIPELINE_BIN)

# Build Open-Meteo + Elpriset parser test
$(TEST_WEATHER_BIN): $(TEST_DIR)/integration/test_openmeteo_parser.c $(TEST_WEATHER_DEPS)
	@echo "Building Open-Meteo parser test..."
	$(CC) $(CFLAGS) -o $@ $(TEST_DIR)/integration/test_openmeteo_parser.c $(TEST_WEATHER_DEPS) $(LDFLAGS)
	@echo "Open-Meteo parser test built: $@"

# Run Open-Meteo + Elpriset parser test
.PHONY: test-weather
test-weather: directories $(TEST_WEATHER_BIN)
	@echo "Running Open-Meteo parser test..."
	@$(TEST_WEATHER_BIN)

# Build JWT Validator test
$(TEST_JWT_BIN): $(TEST_DIR)/unit/test_jwt_validator.c $(TEST_JWT_DEPS)
	@echo "Building JWT Validator test..."
	$(CC) $(CFLAGS) -o $@ $(TEST_DIR)/unit/test_jwt_validator.c $(TEST_JWT_DEPS) $(LDFLAGS)
	@echo "JWT Validator test built: $@"

# Build HTTP Request test
$(TEST_HTTP_REQ_BIN): $(TEST_DIR)/unit/test_http_request.c $(TEST_HTTP_REQ_DEPS)
	@echo "Building HTTP Request test..."
	$(CC) $(CFLAGS) -o $@ $(TEST_DIR)/unit/test_http_request.c $(TEST_HTTP_REQ_DEPS) $(LDFLAGS)
	@echo "HTTP Request test built: $@"

# Build HTTP Response test
$(TEST_HTTP_RESP_BIN): $(TEST_DIR)/unit/test_http_response.c $(TEST_HTTP_RESP_DEPS)
	@echo "Building HTTP Response test..."
	$(CC) $(CFLAGS) -o $@ $(TEST_DIR)/unit/test_http_response.c $(TEST_HTTP_RESP_DEPS) $(LDFLAGS)
	@echo "HTTP Response test built: $@"

# Run JWT Validator test
.PHONY: test-jwt
test-jwt: directories $(TEST_JWT_BIN)
	@echo "Running JWT Validator test..."
	@GRIDGUARD_JWT_SECRET=gridguard-test-secret $(TEST_JWT_BIN)

# Run HTTP Request test
.PHONY: test-http-request
test-http-request: directories $(TEST_HTTP_REQ_BIN)
	@echo "Running HTTP Request test..."
	@$(TEST_HTTP_REQ_BIN)

# Run HTTP Response test
.PHONY: test-http-response
test-http-response: directories $(TEST_HTTP_RESP_BIN)
	@echo "Running HTTP Response test..."
	@$(TEST_HTTP_RESP_BIN)

# Run server
.PHONY: run-server
run-server: server
	@echo "Starting server..."
	@env GRIDGUARD_JWT_SECRET=gridguard-test-secret GRIDGUARD_DB_PATH="$(CURDIR)/gridguard.db" $(SERVER_BIN)

# Starta watchdog i bakgrunden (watchdog startar och övervakar servern)
# GRIDGUARD_JWT_SECRET måste vara satt i miljön.
.PHONY: run-watchdog
run-watchdog: server watchdog
	@if [ -z "$$GRIDGUARD_JWT_SECRET" ]; then \
	    echo "Fel: GRIDGUARD_JWT_SECRET inte satt."; \
	    echo "  export GRIDGUARD_JWT_SECRET=<din-hemliga-nyckel>"; \
	    exit 1; \
	fi
	@echo "Startar watchdog (server övervakas automatiskt)..."
	@setsid env GRIDGUARD_JWT_SECRET="$$GRIDGUARD_JWT_SECRET" GRIDGUARD_DB_PATH="$(CURDIR)/gridguard.db" $(WATCHDOG_BIN) >/dev/null 2>&1 & \
	echo $$! > /tmp/gridguard-watchdog.pid
	@echo "Watchdog igång. Loggar: logs/watchdog.log  ·  logs/server.log"

# Development target - build, start server, and run end-to-end test
.PHONY: dev
dev: server watchdog
	@if [ -f /tmp/gridguard-watchdog.pid ]; then \
	    kill -9 $$(cat /tmp/gridguard-watchdog.pid) 2>/dev/null; rm -f /tmp/gridguard-watchdog.pid; fi
	@pkill -9 GridGuard 2>/dev/null || true
	@fuser -k -9 8080/tcp 2>/dev/null || true
	@rm -f /tmp/gridguard* 2>/dev/null || true
	@sleep 0.5
	@echo ""
	@echo "	GRIDGUARD DEVELOPMENT ENVIRONMENT	"
	@echo ""
	@SECRET=$${GRIDGUARD_JWT_SECRET:-gridguard-test-secret}; \
	echo "Generating JWT token..."; \
	DEV_TOKEN=$$(python3 scripts/generate_jwt.py 2>/dev/null | grep -A1 "JWT Token" | tail -1); \
	if [ -z "$$DEV_TOKEN" ]; then \
	    echo "Warning: Failed to generate token, using fallback"; \
	    DEV_TOKEN="eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiJ0ZXN0X3VzZXIiLCJleHAiOjE4MDM5NzI3NjQsImlhdCI6MTc3MjQzNjc2NH0.SiKMf77hM6vWV1icHWSLotmGDAflrp7xEm7LB-loHHg"; \
	fi; \
	echo "Seeding test data..."; \
	python3 scripts/seed_db.py "$(CURDIR)/gridguard.db" || echo "Warning: seed_db.py failed"; \
	echo "Starting server..."; \
	setsid env GRIDGUARD_JWT_SECRET="$$SECRET" GRIDGUARD_DB_PATH="$(CURDIR)/gridguard.db" $(WATCHDOG_BIN) >/dev/null 2>&1 & \
	echo $$! > /tmp/gridguard-watchdog.pid; \
	printf "Waiting for server"; \
	for i in $$(seq 1 20); do \
	    curl -sf http://localhost:8080/health >/dev/null 2>&1 && break; \
	    printf "."; sleep 0.5; \
	done; \
	echo " ready"; \
	echo ""; \
	ps aux | grep -E "GridGuard-(server|fetcher|parser)" | grep -v grep | awk '{printf "  [PID %s] %s\n", $$2, $$11}'; \
	echo ""; \
	ls -lh /tmp/gridguard* 2>/dev/null | awk '{printf "  %s\n", $$9}' || true; \
	echo ""; \
	echo "Server:   http://localhost:8080"; \
	echo "Database: gridguard.db"; \
	echo "Logs:     logs/*.log"; \
	echo ""; \
	echo "Running test forecast request..."; \
	echo ""; \
	curl -s -X GET "http://localhost:8080/forecast" \
	    -H "Authorization: Bearer $$DEV_TOKEN" | python3 -m json.tool 2>/dev/null || echo "Request failed"; \
	echo ""; \
	echo "Running load shift example (EV charger, 40 kWh, 11 kW, deadline 07:00 tomorrow)..."; \
	echo ""; \
	DEADLINE=$$(python3 -c "import time,datetime; t=datetime.datetime.now(datetime.timezone.utc)+datetime.timedelta(days=1); print(int(datetime.datetime(t.year,t.month,t.day,7,0,0,tzinfo=datetime.timezone.utc).timestamp()))"); \
	curl -s -X POST "http://localhost:8080/schedule" \
	    -H "Authorization: Bearer $$DEV_TOKEN" \
	    -H "Content-Type: application/json" \
	    -d "{\"load_id\":\"ev_charger\",\"duration_minutes\":216,\"power_kw\":11.0,\"deadline\":$$DEADLINE}" \
	    | python3 -m json.tool 2>/dev/null || echo "Request failed"; \
	echo ""; \

# Stop all GridGuard processes and clean up IPC resources
.PHONY: stop
stop:
	@echo "Stopping GridGuard..."
	@if [ -f /tmp/gridguard-watchdog.pid ]; then \
	    PID=$$(cat /tmp/gridguard-watchdog.pid 2>/dev/null); \
	    if [ -n "$$PID" ] && kill -0 $$PID 2>/dev/null; then \
	        kill -9 $$PID 2>/dev/null && echo "  Killed watchdog (PID $$PID)"; \
	    fi; \
	    rm -f /tmp/gridguard-watchdog.pid; \
	fi
	@if [ -f /tmp/gridguard.pid ]; then \
	    PID=$$(cat /tmp/gridguard.pid 2>/dev/null); \
	    if [ -n "$$PID" ] && kill -0 $$PID 2>/dev/null; then \
	        kill -9 $$PID 2>/dev/null && echo "  Killed server (PID $$PID)"; \
	    fi; \
	    rm -f /tmp/gridguard.pid; \
	fi
	@pkill -9 -f GridGuard-fetcher 2>/dev/null && echo "  Killed fetcher" || true
	@pkill -9 -f GridGuard-parser 2>/dev/null && echo "  Killed parser" || true
	@pkill -9 GridGuard-server 2>/dev/null && echo "  Killed remaining servers" || true
	@fuser -k -9 8080/tcp 2>/dev/null && echo "  Freed port 8080" || true
	@rm -f /tmp/gridguard_fetch_to_parse.fifo /tmp/gridguard_parse_to_compute.sock /tmp/gridguard.status /tmp/gridguard*.pid 2>/dev/null || true
	@echo "Stopped"

# Memory leak check with Valgrind
.PHONY: valgrind-server
valgrind-server: debug
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes $(SERVER_BIN)

# Thread safety check with Helgrind
.PHONY: helgrind
helgrind: debug
	valgrind --tool=helgrind $(SERVER_BIN)

# Profile with gprof (kör efter att programmet avslutat)
.PHONY: gprof-analyze
gprof-analyze:
	@if [ -f gmon.out ]; then \
		gprof $(SERVER_BIN) gmon.out > profile_report.txt; \
		echo "Profile report saved to profile_report.txt"; \
	else \
		echo "No gmon.out file found. Run the program with profiling enabled first."; \
	fi

# Clean build artifacts
.PHONY: clean
clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILD_DIR)
	rm -rf $(BIN_DIR)
	rm -f gmon.out
	rm -f *.gcda *.gcno *.gcov
	rm -f profile_report.txt
	rm -f vgcore.*
	@echo "Clean complete"

# Clean everything including logs and config
.PHONY: distclean
distclean: clean
	rm -rf logs/*.log
	rm -f *.sock
	rm -f *.pid
	@echo "Distribution clean complete"

# Install (om ni vill ha installation)
.PHONY: install
install: release
	@echo "Installing LEOP..."
	@# Lägg till installation commands här
	@echo "Installation not implemented yet"

# Cleanup för IPC-resurser (pipes, sockets, shared memory)
.PHONY: clean-ipc
clean-ipc:
	@echo "Cleaning IPC resources..."
	@rm -f /tmp/gridguard_*.fifo
	@rm -f /tmp/gridguard*.sock
	@rm -f /dev/shm/gridguard_*
	@rm -f /dev/shm/sem.gridguard_*
	@echo "IPC resources cleaned"

# Help target
.PHONY: help
help:
	@echo "GridGuard Makefile targets:"
	@echo ""
	@echo "  all          - Build process-based IPC architecture (3 executables)"
	@echo "  server       - Build server, fetcher, and parser"
	@echo "  client       - Build client"
	@echo "  watchdog     - Build watchdog"
	@echo "  debug        - Build with debug symbols and no optimization"
	@echo "  release      - Build optimized release version"
	@echo "  profile      - Build with profiling support"
	@echo "  coverage     - Build with code coverage support"
	@echo ""
	@echo "  test         - Run all tests"
	@echo "  test-api     - Run API fetch and parser tests"
	@echo "  test-logger  - Run logger tests"
	@echo "  test-pipeline- Run multi-threaded pipeline tests"
	@echo "  valgrind-*   - Run Valgrind memory check"
	@echo "  helgrind     - Run Helgrind thread safety check"
	@echo "  gprof-analyze- Analyze gprof profiling data"
	@echo ""
	@echo "  dev          - Run development server with watchdog"
	@echo "  stop         - Stop server and watchdog"
	@echo "  clean-ipc    - Clean IPC resources (pipes, sockets, shm)"
	@echo ""
	@echo "  clean        - Remove build artifacts"
	@echo "  distclean    - Remove all generated files"
	@echo "  install      - Install binaries (not implemented)"
	@echo ""
	@echo "  help         - Show this help message"

# Phony targets (inte verkliga filer)
.PHONY: all debug release profile coverage test clean distclean install help

# Dependencies (auto-generated)
-include $(SERVER_OBJS:.o=.d)

