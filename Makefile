# GridGuard Makefile
# Systemprogrammering och introduktion till C++

# Compiler och flaggor
CC = gcc
CXX = g++
LDFLAGS = -pthread -lcurl

# Directories
SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin
SERVER_DIR = $(SRC_DIR)/server
CLIENT_DIR = $(SRC_DIR)/client
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
WATCHDOG_DIR = $(INFRASTRUCTURE_DIR)/watchdog

# Network directories
NETWORK_DIR = $(SRC_DIR)/network
NETWORK_SERVER_DIR = $(NETWORK_DIR)/server
NETWORK_CLIENT_DIR = $(NETWORK_DIR)/client

# Concurrency directories (OS primitives)
CONCURRENCY_DIR = $(SRC_DIR)/concurrency
THREADS_DIR = $(CONCURRENCY_DIR)/threads
SYNC_DIR = $(CONCURRENCY_DIR)/sync
IPC_DIR = $(CONCURRENCY_DIR)/ipc

# Include paths for headers
INCLUDES = -I$(SRC_DIR) \
           -I$(SERVER_DIR) \
           -I$(CLIENT_DIR) \
           -I$(LIBS_DIR) \
           -I$(APPLICATION_DIR) \
           -I$(APP_CORE_DIR) \
           -I$(APP_WORKERS_DIR) \
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
           -I$(NETWORK_DIR) \
           -I$(NETWORK_SERVER_DIR) \
           -I$(NETWORK_CLIENT_DIR) \
           -I$(CONCURRENCY_DIR) \
           -I$(THREADS_DIR) \
           -I$(SYNC_DIR) \
           -I$(IPC_DIR)

# Compiler flags
CFLAGS = -Wall -Wextra -Werror -std=c11 -pthread -g $(INCLUDES)
CXXFLAGS = -Wall -Wextra -Werror -std=c++17 -pthread -g $(INCLUDES)

# Output binaries
SERVER_BIN = $(BIN_DIR)/GridGuard-server
CLIENT_BIN = $(BIN_DIR)/GridGuard-client
WATCHDOG_BIN = $(BIN_DIR)/GridGuard-watchdog

# Source files
SERVER_SRCS_C = $(wildcard $(SERVER_DIR)/*.c) \
                $(wildcard $(LOGGING_DIR)/*.c) \
                $(wildcard $(SIGNALS_DIR)/*.c) \
                $(wildcard $(DAEMON_DIR)/*.c) \
                $(wildcard $(NETWORK_SERVER_DIR)/*.c) \
                $(wildcard $(APP_API_DIR)/*.c) \
                $(wildcard $(APP_CORE_DIR)/*.c) \
                $(wildcard $(APP_WORKERS_DIR)/*.c) \
                $(wildcard $(APP_MODELS_DOMAIN_DIR)/*.c) \
                $(wildcard $(APP_SERVICES_DIR)/*.c) \
                $(wildcard $(THREADS_DIR)/*.c) \
                $(wildcard $(SYNC_DIR)/*.c) \
                $(wildcard $(IPC_DIR)/*.c) \
                $(wildcard $(LIBS_DIR)/*.c)

SERVER_SRCS_CPP = $(wildcard $(NETWORK_CLIENT_DIR)/*.cpp)

CLIENT_SRCS = $(wildcard $(CLIENT_DIR)/*.cpp) \
              $(wildcard $(LOGGING_DIR)/*.c) \
              $(wildcard $(NETWORK_CLIENT_DIR)/*.cpp)

TEST_SRCS = $(wildcard $(TEST_DIR)/unit/*.c) \
            $(wildcard $(TEST_DIR)/integration/*.c)

# Object files
SERVER_OBJS = $(SERVER_SRCS_C:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o) $(SERVER_SRCS_CPP:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
CLIENT_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(filter %.c,$(CLIENT_SRCS))) $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(filter %.cpp,$(CLIENT_SRCS)))
TEST_OBJS = $(TEST_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Test binary
TEST_BIN = $(BIN_DIR)/test_runner

# Watchdog source files
WATCHDOG_SRCS = $(WATCHDOG_DIR)/main.c \
                $(WATCHDOG_DIR)/Watchdog.c \
                $(DAEMON_DIR)/PidFile.c \
                $(LOGGING_DIR)/Logger.c

WATCHDOG_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(WATCHDOG_SRCS))

# Default target
.PHONY: all
all: directories server client

# Individual targets for CI
.PHONY: server
server: directories $(SERVER_BIN)

.PHONY: client
client: directories $(CLIENT_BIN)

# Create necessary directories
.PHONY: directories
directories:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/server
	@mkdir -p $(BUILD_DIR)/client
	@mkdir -p $(BUILD_DIR)/application/core
	@mkdir -p $(BUILD_DIR)/application/workers
	@mkdir -p $(BUILD_DIR)/application/models/apis
	@mkdir -p $(BUILD_DIR)/application/models/domain
	@mkdir -p $(BUILD_DIR)/application/models/config
	@mkdir -p $(BUILD_DIR)/application/services
	@mkdir -p $(BUILD_DIR)/application/api
	@mkdir -p $(BUILD_DIR)/application/configs
	@mkdir -p $(BUILD_DIR)/infrastructure/logging
	@mkdir -p $(BUILD_DIR)/infrastructure/signals
	@mkdir -p $(BUILD_DIR)/infrastructure/daemon
	@mkdir -p $(BUILD_DIR)/infrastructure/watchdog
	@mkdir -p $(BUILD_DIR)/network/server
	@mkdir -p $(BUILD_DIR)/network/client
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

# Build client
$(CLIENT_BIN): $(CLIENT_OBJS)
	@echo "Linking client..."
	$(CXX) -o $@ $^ $(LDFLAGS)
	@echo "Client built successfully: $@"

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
test: test-api test-logger test-pipeline test-weather
	@echo ""
	@echo "======================================"
	@echo "All tests passed!"
	@echo "======================================"

# Test binaries
TEST_API_BIN = $(BIN_DIR)/test_api_fetch
TEST_LOGGER_BIN = $(BIN_DIR)/test_logger
TEST_PIPELINE_BIN = $(BIN_DIR)/test_pipeline
TEST_WEATHER_BIN = $(BIN_DIR)/test_multi_source_weather

# Test dependencies
TEST_API_DEPS = $(wildcard $(APP_API_DIR)/*.c) \
                $(wildcard $(LOGGING_DIR)/*.c) \
                $(wildcard $(APP_SERVICES_DIR)/*.c) \
                $(wildcard $(APP_MODELS_DOMAIN_DIR)/*.c) \
                $(wildcard $(LIBS_DIR)/*.c)

TEST_LOGGER_DEPS = $(LOGGING_DIR)/Logger.c

TEST_PIPELINE_DEPS = $(wildcard $(APP_CORE_DIR)/*.c) \
                     $(wildcard $(APP_WORKERS_DIR)/*.c) \
                     $(wildcard $(APP_SERVICES_DIR)/*.c) \
                     $(wildcard $(SYNC_DIR)/*.c) \
                     $(wildcard $(APP_API_DIR)/*.c) \
                     $(wildcard $(LOGGING_DIR)/*.c) \
                     $(wildcard $(APP_MODELS_DOMAIN_DIR)/*.c) \
                     $(wildcard $(LIBS_DIR)/*.c)

TEST_WEATHER_DEPS = $(wildcard $(APP_API_DIR)/*.c) \
                    $(wildcard $(LOGGING_DIR)/*.c) \
                    $(wildcard $(APP_SERVICES_DIR)/*.c) \
                    $(wildcard $(APP_MODELS_DOMAIN_DIR)/*.c) \
                    $(wildcard $(LIBS_DIR)/*.c)

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

# Build Multi-Source Weather test
$(TEST_WEATHER_BIN): $(TEST_DIR)/integration/test_multi_source_weather.c $(TEST_WEATHER_DEPS)
	@echo "Building Multi-Source Weather test..."
	$(CC) $(CFLAGS) -o $@ $(TEST_DIR)/integration/test_multi_source_weather.c $(TEST_WEATHER_DEPS) $(LDFLAGS)
	@echo "Multi-Source Weather test built: $@"

# Run Multi-Source Weather test
.PHONY: test-weather
test-weather: directories $(TEST_WEATHER_BIN)
	@echo "Running Multi-Source Weather test..."
	@$(TEST_WEATHER_BIN)

# Run server
.PHONY: run-server
run-server: server
	@echo "Starting server..."
	@$(SERVER_BIN)

# Run client
.PHONY: run-client
run-client: client
	@echo "Starting client..."
	@$(CLIENT_BIN)

# Run watchdog (starts daemon automatically)
.PHONY: run-watchdog
run-watchdog: server watchdog
	@echo "Starting watchdog..."
	@$(WATCHDOG_BIN)

# Run both (server in background, client in foreground)
.PHONY: run
run: all
	@echo "Starting server in background..."
	@$(SERVER_BIN) &
	@sleep 1
	@echo "Starting client..."
	@$(CLIENT_BIN)

# Memory leak check with Valgrind
.PHONY: valgrind-server
valgrind-server: debug
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes $(SERVER_BIN)

.PHONY: valgrind-client
valgrind-client: debug
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes $(CLIENT_BIN)

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

# Help target
.PHONY: help
help:
	@echo "LEOP Makefile targets:"
	@echo ""
	@echo "  all          - Build both server and client (default)"
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
	@echo "  clean        - Remove build artifacts"
	@echo "  distclean    - Remove all generated files"
	@echo "  install      - Install binaries (not implemented)"
	@echo ""
	@echo "  help         - Show this help message"

# Phony targets (inte verkliga filer)
.PHONY: all debug release profile coverage test clean distclean install help

# Dependencies (auto-generated)
-include $(SERVER_OBJS:.o=.d)
-include $(CLIENT_OBJS:.o=.d)

