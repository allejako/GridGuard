#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

extern "C" {
    #include "sys/Logger.h"
}

class LoggerTest : public ::testing::Test {
protected:
    const char* testLogFile = "/tmp/gridguard_test.log";

    void SetUp() override {
        // Clean up any existing test log file
        std::remove(testLogFile);
    }

    void TearDown() override {
        Logger_Shutdown();
        std::remove(testLogFile);
    }
};

TEST_F(LoggerTest, InitiateWithNullPath) {
    // Should succeed and log to stdout only
    int result = Logger_Initiate(nullptr, LOG_LEVEL_INFO);
    EXPECT_EQ(result, 0);
}

TEST_F(LoggerTest, InitiateWithFilePath) {
    int result = Logger_Initiate(testLogFile, LOG_LEVEL_DEBUG);
    EXPECT_EQ(result, 0);

    // File should exist
    std::ifstream file(testLogFile);
    EXPECT_TRUE(file.good());
}

TEST_F(LoggerTest, SetAndGetLevel) {
    Logger_Initiate(nullptr, LOG_LEVEL_INFO);

    // Default level should be INFO
    EXPECT_EQ(Logger_GetLevel(), LOG_LEVEL_INFO);

    // Change to DEBUG
    Logger_SetLevel(LOG_LEVEL_DEBUG);
    EXPECT_EQ(Logger_GetLevel(), LOG_LEVEL_DEBUG);

    // Change to ERROR
    Logger_SetLevel(LOG_LEVEL_ERROR);
    EXPECT_EQ(Logger_GetLevel(), LOG_LEVEL_ERROR);
}

TEST_F(LoggerTest, LogToFile) {
    Logger_Initiate(testLogFile, LOG_LEVEL_DEBUG);

    LOG_INFO("Test message: %s", "Hello GridGuard");
    LOG_ERROR("Error code: %d", 42);

    Logger_Shutdown();

    // Read log file
    std::ifstream file(testLogFile);
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    // Verify messages are in the log
    EXPECT_TRUE(content.find("Test message: Hello GridGuard") != std::string::npos);
    EXPECT_TRUE(content.find("Error code: 42") != std::string::npos);
    EXPECT_TRUE(content.find("INFO") != std::string::npos);
    EXPECT_TRUE(content.find("ERROR") != std::string::npos);
}

TEST_F(LoggerTest, LogLevelFiltering) {
    Logger_Initiate(testLogFile, LOG_LEVEL_WARNING);

    // These should be filtered out
    LOG_DEBUG("Debug message");
    LOG_INFO("Info message");

    // These should appear
    LOG_WARNING("Warning message");
    LOG_ERROR("Error message");

    Logger_Shutdown();

    // Read log file
    std::ifstream file(testLogFile);
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    // Verify filtering
    EXPECT_TRUE(content.find("Debug message") == std::string::npos);
    EXPECT_TRUE(content.find("Info message") == std::string::npos);
    EXPECT_TRUE(content.find("Warning message") != std::string::npos);
    EXPECT_TRUE(content.find("Error message") != std::string::npos);
}

TEST_F(LoggerTest, MultipleShutdowns) {
    Logger_Initiate(testLogFile, LOG_LEVEL_INFO);
    Logger_Shutdown();

    // Should not crash
    Logger_Shutdown();
    Logger_Shutdown();
}

TEST_F(LoggerTest, LogAllLevels) {
    Logger_Initiate(testLogFile, LOG_LEVEL_DEBUG);

    LOG_DEBUG("Debug level");
    LOG_INFO("Info level");
    LOG_WARNING("Warning level");
    LOG_ERROR("Error level");
    LOG_FATAL("Fatal level");

    Logger_Shutdown();

    std::ifstream file(testLogFile);
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    EXPECT_TRUE(content.find("DEBUG") != std::string::npos);
    EXPECT_TRUE(content.find("INFO") != std::string::npos);
    EXPECT_TRUE(content.find("WARNING") != std::string::npos);
    EXPECT_TRUE(content.find("ERROR") != std::string::npos);
    EXPECT_TRUE(content.find("FATAL") != std::string::npos);
}
