// Heartbeat Tests
// Verifies the pipe-based health-check primitive used by the watchdog

#include <gtest/gtest.h>
#include <unistd.h>

extern "C" {
#include "watchdog/Heartbeat.h"
}

TEST(HeartbeatTest, InitiateCreatesPipe) {
    Heartbeat hb;
    ASSERT_EQ(Heartbeat_Initiate(&hb), 0);
    EXPECT_GE(hb.read_fd,  0);
    EXPECT_GE(hb.write_fd, 0);
    Heartbeat_Shutdown(&hb);
}

TEST(HeartbeatTest, GetWriteFdMatchesStruct) {
    Heartbeat hb;
    ASSERT_EQ(Heartbeat_Initiate(&hb), 0);
    EXPECT_EQ(Heartbeat_GetWriteFd(&hb), hb.write_fd);
    Heartbeat_Shutdown(&hb);
}

// Writing a byte to the pipe should make Check() return 1 immediately
TEST(HeartbeatTest, CheckReturnsTrueAfterWrite) {
    Heartbeat hb;
    ASSERT_EQ(Heartbeat_Initiate(&hb), 0);

    char beat = 1;
    write(hb.write_fd, &beat, 1);

    EXPECT_EQ(Heartbeat_Check(&hb, 1), 1);
    Heartbeat_Shutdown(&hb);
}

// With nothing written, a 0-second timeout must return 0 (no beat detected)
TEST(HeartbeatTest, CheckReturnsZeroOnTimeout) {
    Heartbeat hb;
    ASSERT_EQ(Heartbeat_Initiate(&hb), 0);

    EXPECT_EQ(Heartbeat_Check(&hb, 0), 0);
    Heartbeat_Shutdown(&hb);
}

// CloseWriteFd must set write_fd to -1
TEST(HeartbeatTest, CloseWriteFdSetsMinusOne) {
    Heartbeat hb;
    ASSERT_EQ(Heartbeat_Initiate(&hb), 0);

    EXPECT_EQ(Heartbeat_CloseWriteFd(&hb), 0);
    EXPECT_EQ(hb.write_fd, -1);

    // Only read_fd remains open; clean up manually
    close(hb.read_fd);
}

// CloseReadFd must set read_fd to -1
TEST(HeartbeatTest, CloseReadFdSetsMinusOne) {
    Heartbeat hb;
    ASSERT_EQ(Heartbeat_Initiate(&hb), 0);

    EXPECT_EQ(Heartbeat_CloseReadFd(&hb), 0);
    EXPECT_EQ(hb.read_fd, -1);

    close(hb.write_fd);
}

// After consuming a beat, the pipe is empty until another beat is written
TEST(HeartbeatTest, BeatIsConsumedOnRead) {
    Heartbeat hb;
    ASSERT_EQ(Heartbeat_Initiate(&hb), 0);

    char beat = 1;
    write(hb.write_fd, &beat, 1);
    EXPECT_EQ(Heartbeat_Check(&hb, 0), 1); // beat consumed

    // Pipe is now empty — second check must timeout
    EXPECT_EQ(Heartbeat_Check(&hb, 0), 0);

    // Write another beat; it must be detected
    write(hb.write_fd, &beat, 1);
    EXPECT_EQ(Heartbeat_Check(&hb, 0), 1);

    Heartbeat_Shutdown(&hb);
}

// Null pointer inputs must not crash and return sensible error values
TEST(HeartbeatTest, NullPointerSafety) {
    EXPECT_EQ(Heartbeat_Initiate(nullptr),   -1);
    EXPECT_EQ(Heartbeat_GetWriteFd(nullptr), -1);
    // These must be no-ops:
    Heartbeat_Shutdown(nullptr);
    Heartbeat_CloseWriteFd(nullptr);
    Heartbeat_CloseReadFd(nullptr);
}
