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
