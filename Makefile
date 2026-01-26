# LEOP Makefile
# Systemprogrammering och introduktion till C++

# Compiler och flaggor
CC = gcc
CXX = g++
LDFLAGS = -pthread

# Directories
SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin
SERVER_DIR = $(SRC_DIR)/server
CLIENT_DIR = $(SRC_DIR)/client
COMMON_DIR = $(SRC_DIR)/common
TCP_DIR = $(SRC_DIR)/tcp
THREADS_DIR = $(SRC_DIR)/threads
TEST_DIR = $(SRC_DIR)/tests
LIBS_DIR = $(SRC_DIR)/libs

# Include paths for headers
INCLUDES = -I$(SRC_DIR) -I$(COMMON_DIR) -I$(TCP_DIR) -I$(THREADS_DIR) -I$(SERVER_DIR) -I$(CLIENT_DIR) -I$(LIBS_DIR)

# Compiler flags
CFLAGS = -Wall -Wextra -Werror -std=c11 -pthread -g $(INCLUDES)
CXXFLAGS = -Wall -Wextra -Werror -std=c++17 -pthread -g $(INCLUDES)

# Output binaries
SERVER_BIN = $(BIN_DIR)/leop-server
CLIENT_BIN = $(BIN_DIR)/leop-client

# Source files (lägg till fler när de skapas)
SERVER_SRCS = $(wildcard $(SERVER_DIR)/*.c) $(wildcard $(COMMON_DIR)/*.c) $(wildcard $(TCP_DIR)/*.c) $(wildcard $(THREADS_DIR)/*.c)
CLIENT_SRCS = $(wildcard $(CLIENT_DIR)/*.c) $(wildcard $(CLIENT_DIR)/*.cpp) $(wildcard $(COMMON_DIR)/*.c) $(wildcard $(COMMON_DIR)/*.cpp) $(wildcard $(TCP_DIR)/*.c)
TEST_SRCS = $(wildcard $(TEST_DIR)/*.c)

# Object files
SERVER_OBJS = $(SERVER_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
CLIENT_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(filter %.c,$(CLIENT_SRCS))) $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(filter %.cpp,$(CLIENT_SRCS)))
TEST_OBJS = $(TEST_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Test binary
TEST_BIN = $(BIN_DIR)/test_runner

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
	@mkdir -p $(BUILD_DIR)/common
	@mkdir -p $(BUILD_DIR)/tcp
	@mkdir -p $(BUILD_DIR)/threads
	@mkdir -p $(BUILD_DIR)/tests
	@mkdir -p $(BIN_DIR)
	@mkdir -p logs
	@mkdir -p config

# Build server
$(SERVER_BIN): $(SERVER_OBJS)
	@echo "Linking server..."
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Server built successfully: $@"

# Build client
$(CLIENT_BIN): $(CLIENT_OBJS)
	@echo "Linking client..."
	$(CC) $(LDFLAGS) -o $@ $^
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

# Run tests
.PHONY: test
test: directories $(TEST_BIN)
	@echo "Running tests..."
	@$(TEST_BIN) || (echo "Tests failed!" && exit 1)
	@echo "All tests passed!"

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

