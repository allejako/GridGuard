# Unit tests
add_executable(test_logger_gtest
    unit/test_logger_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/sys/Logger.c
)

target_link_libraries(test_logger_gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(test_logger_gtest)

# HTTP Request tests
add_executable(test_http_request_gtest
    unit/test_http_request_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/net/HTTPRequest.c
    ${CMAKE_SOURCE_DIR}/src/sys/Logger.c
)

target_link_libraries(test_http_request_gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(test_http_request_gtest)

# HTTP Response tests
add_executable(test_http_response_gtest
    unit/test_http_response_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/net/HTTPResponse.c
    ${CMAKE_SOURCE_DIR}/src/sys/Logger.c
)

target_link_libraries(test_http_response_gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(test_http_response_gtest)

# ==============================================================================
# GridGuard-Specific Tests (Resilience, Energy Optimization, Concurrency)
# ==============================================================================

# Watchdog Resilience Tests - RestartPolicy exponential backoff & rate limiting
add_executable(test_restart_policy_gtest
    unit/test_restart_policy_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/watchdog/RestartPolicy.c
    ${CMAKE_SOURCE_DIR}/src/sys/Logger.c
)

target_link_libraries(test_restart_policy_gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(test_restart_policy_gtest)

# Energy-Aware Scheduling Tests - LoadScheduler spot price optimization
add_executable(test_scheduler_gtest
    unit/test_scheduler_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/domain/LoadScheduler.c
    ${CMAKE_SOURCE_DIR}/src/sys/Logger.c
)

target_link_libraries(test_scheduler_gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(test_scheduler_gtest)

# ISO 8601 Timestamp Parser Tests - Correct UTC conversion (fixes 04:45 solar bug)
add_executable(test_parser_gtest
    unit/test_parser_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/parser/Parser.c
    ${CMAKE_SOURCE_DIR}/src/api/APIParser.c
    ${CMAKE_SOURCE_DIR}/src/sys/Logger.c
    ${CMAKE_SOURCE_DIR}/src/sys/ProcessHeartbeat.c
    ${CMAKE_SOURCE_DIR}/src/libs/cJSON.c
)

target_link_libraries(test_parser_gtest
    GTest::gtest_main
    Threads::Threads
    OpenSSL::SSL OpenSSL::Crypto
)

gtest_discover_tests(test_parser_gtest)

# Concurrent Queue Stress Tests - Multi-threaded work distribution
add_executable(test_queue_concurrent_gtest
    unit/test_queue_concurrent_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/sys/Queue.c
    ${CMAKE_SOURCE_DIR}/src/sys/Logger.c
)

target_link_libraries(test_queue_concurrent_gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(test_queue_concurrent_gtest)

# Config Parser Tests - INI configuration file parsing
add_executable(test_config_parser_gtest
    unit/test_config_parser_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/config/ConfigParser.c
    ${CMAKE_SOURCE_DIR}/src/sys/Logger.c
)

target_link_libraries(test_config_parser_gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(test_config_parser_gtest)

# ==============================================================================
# Extended Coverage Tests (Security, Database, Compute, Synchronization)
# ==============================================================================

# Locate mbedTLS and SQLite3 libraries needed by the extended tests
find_library(MBEDTLS_LIB    NAMES mbedtls)
find_library(MBEDX509_LIB   NAMES mbedx509)
find_library(MBEDCRYPTO_LIB NAMES mbedcrypto)
find_library(SQLITE3_LIB    NAMES sqlite3)

# JWT Tests - token issuance and signature validation (JWTIssuer + JWTValidator)
add_executable(test_jwt_gtest
    unit/test_jwt_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/auth/JWTIssuer.c
    ${CMAKE_SOURCE_DIR}/src/auth/JWTValidator.c
    ${CMAKE_SOURCE_DIR}/src/sys/Logger.c
)

target_link_libraries(test_jwt_gtest
    GTest::gtest_main
    Threads::Threads
    ${MBEDTLS_LIB} ${MBEDX509_LIB} ${MBEDCRYPTO_LIB}
)

gtest_discover_tests(test_jwt_gtest)

# RuntimeConfig Tests - three-level fallback chain (file → env → default)
add_executable(test_runtime_config_gtest
    unit/test_runtime_config_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/config/RuntimeConfig.c
    ${CMAKE_SOURCE_DIR}/src/config/ConfigParser.c
    ${CMAKE_SOURCE_DIR}/src/sys/Logger.c
)

target_link_libraries(test_runtime_config_gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(test_runtime_config_gtest)

# API Endpoint URL Builder Tests
add_executable(test_api_endpoints_gtest
    unit/test_api_endpoints_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/api/APIEndpoints.c
    ${CMAKE_SOURCE_DIR}/src/sys/Logger.c
)

target_link_libraries(test_api_endpoints_gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(test_api_endpoints_gtest)

# Compute Energy Plan Tests - BUY/SELL/AVOID/IDLE signal generation
add_executable(test_compute_gtest
    unit/test_compute_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/compute/Compute.c
    ${CMAKE_SOURCE_DIR}/src/sys/Logger.c
)

target_link_libraries(test_compute_gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(test_compute_gtest)

# WorkCompletion Tests - one-shot signal/wait synchronization primitive
add_executable(test_work_completion_gtest
    unit/test_work_completion_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/sys/WorkCompletion.c
    ${CMAKE_SOURCE_DIR}/src/sys/Logger.c
)

target_link_libraries(test_work_completion_gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(test_work_completion_gtest)

# Heartbeat Tests - pipe-based process health-check
add_executable(test_heartbeat_gtest
    unit/test_heartbeat_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/watchdog/Heartbeat.c
    ${CMAKE_SOURCE_DIR}/src/sys/Logger.c
)

target_link_libraries(test_heartbeat_gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(test_heartbeat_gtest)

# ScheduleDB Tests - CRUD for scheduled loads (in-memory SQLite)
add_executable(test_schedule_db_gtest
    unit/test_schedule_db_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/db/ScheduleDB.c
    ${CMAKE_SOURCE_DIR}/src/db/ClientDB.c
    ${CMAKE_SOURCE_DIR}/src/sys/Logger.c
)

target_link_libraries(test_schedule_db_gtest
    GTest::gtest_main
    Threads::Threads
    ${SQLITE3_LIB}
)

gtest_discover_tests(test_schedule_db_gtest)

# UserConfigDB Tests - user location/solar/grid-fee persistence (in-memory SQLite)
add_executable(test_user_config_db_gtest
    unit/test_user_config_db_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/db/UserConfigDB.c
    ${CMAKE_SOURCE_DIR}/src/db/ClientDB.c
    ${CMAKE_SOURCE_DIR}/src/sys/Logger.c
)

target_link_libraries(test_user_config_db_gtest
    GTest::gtest_main
    Threads::Threads
    ${SQLITE3_LIB}
)

gtest_discover_tests(test_user_config_db_gtest)

# ==============================================================================
# Integration Tests (Multi-Process Pipeline & Chaos Engineering)
# ==============================================================================

# Process Pipeline Integration Tests
# Tests the full fork()/exec() process boundary and heartbeat IPC
add_executable(test_process_pipeline_gtest
    integration/test_process_pipeline_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/watchdog/Heartbeat.c
    ${CMAKE_SOURCE_DIR}/src/watchdog/RestartPolicy.c
    ${CMAKE_SOURCE_DIR}/src/sys/Logger.c
)

target_link_libraries(test_process_pipeline_gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(test_process_pipeline_gtest)

# Chaos Engineering Tests
# SIGKILL scenarios and watchdog recovery verification
add_executable(test_chaos_watchdog_gtest
    integration/test_chaos_watchdog_gtest.cpp
    ${CMAKE_SOURCE_DIR}/src/watchdog/Heartbeat.c
    ${CMAKE_SOURCE_DIR}/src/watchdog/RestartPolicy.c
    ${CMAKE_SOURCE_DIR}/src/sys/Logger.c
)

target_link_libraries(test_chaos_watchdog_gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(test_chaos_watchdog_gtest)
