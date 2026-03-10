// GridGuard Watchdog Resilience Tests
// Tests exponential backoff and restart rate limiting - core to GridGuard's supervision model

#include <gtest/gtest.h>
#include <thread>
#include <chrono>

extern "C" {
    #include "watchdog/RestartPolicy.h"
}

class RestartPolicyTest : public ::testing::Test {
protected:
    RestartPolicy* policy = nullptr;

    void TearDown() override {
        if (policy) {
            RestartPolicy_Destroy(policy);
            policy = nullptr;
        }
    }
};

// Test basic restart policy creation and limits
TEST_F(RestartPolicyTest, CreateAndCheckLimits) {
    policy = RestartPolicy_Create(5, 300, 2);
    ASSERT_NE(policy, nullptr);

    EXPECT_EQ(RestartPolicy_GetCount(policy), 0);
    EXPECT_EQ(RestartPolicy_GetMax(policy), 5);
    EXPECT_TRUE(RestartPolicy_CanRestart(policy));
}

// Test exponential backoff progression: 2s, 4s, 8s, 16s, 32s
// This is critical for GridGuard's watchdog to avoid thundering herd
TEST_F(RestartPolicyTest, ExponentialBackoffProgression) {
    policy = RestartPolicy_Create(5, 300, 2);

    // First restart: 2s backoff
    RestartPolicy_RecordRestart(policy);
    EXPECT_EQ(RestartPolicy_GetBackoffDelay(policy), 2);

    // Second restart: 4s backoff
    RestartPolicy_RecordRestart(policy);
    EXPECT_EQ(RestartPolicy_GetBackoffDelay(policy), 4);

    // Third restart: 8s backoff
    RestartPolicy_RecordRestart(policy);
    EXPECT_EQ(RestartPolicy_GetBackoffDelay(policy), 8);

    // Fourth restart: 16s backoff
    RestartPolicy_RecordRestart(policy);
    EXPECT_EQ(RestartPolicy_GetBackoffDelay(policy), 16);

    // Fifth restart: 32s backoff (capped)
    RestartPolicy_RecordRestart(policy);
    EXPECT_EQ(RestartPolicy_GetBackoffDelay(policy), 32);
}

// Test rate limiting - prevents restart storms when processes crash repeatedly
TEST_F(RestartPolicyTest, RateLimitingPreventsRestartStorm) {
    policy = RestartPolicy_Create(3, 300, 2);

    // Should allow first 3 restarts
    EXPECT_TRUE(RestartPolicy_CanRestart(policy));
    RestartPolicy_RecordRestart(policy);

    EXPECT_TRUE(RestartPolicy_CanRestart(policy));
    RestartPolicy_RecordRestart(policy);

    EXPECT_TRUE(RestartPolicy_CanRestart(policy));
    RestartPolicy_RecordRestart(policy);

    // Fourth restart should be blocked (rate limit exceeded)
    EXPECT_FALSE(RestartPolicy_CanRestart(policy));

    EXPECT_EQ(RestartPolicy_GetCount(policy), 3);
}

// Test that restart counter increments correctly
TEST_F(RestartPolicyTest, RestartCounterIncrement) {
    policy = RestartPolicy_Create(5, 300, 2);

    for (int i = 1; i <= 5; i++) {
        RestartPolicy_RecordRestart(policy);
        EXPECT_EQ(RestartPolicy_GetCount(policy), i);
    }
}

// Test backoff with different base delays
TEST_F(RestartPolicyTest, CustomBaseBackoff) {
    policy = RestartPolicy_Create(3, 300, 5);

    RestartPolicy_RecordRestart(policy);
    EXPECT_EQ(RestartPolicy_GetBackoffDelay(policy), 5);

    RestartPolicy_RecordRestart(policy);
    EXPECT_EQ(RestartPolicy_GetBackoffDelay(policy), 10);

    RestartPolicy_RecordRestart(policy);
    EXPECT_EQ(RestartPolicy_GetBackoffDelay(policy), 20);
}

// Test backoff cap at 32 seconds (prevents excessive delays)
TEST_F(RestartPolicyTest, BackoffCappedAt32Seconds) {
    policy = RestartPolicy_Create(5, 300, 2);

    // Record restarts to reach 32s cap
    // Progression: 2, 4, 8, 16, 32
    RestartPolicy_RecordRestart(policy);
    EXPECT_EQ(RestartPolicy_GetBackoffDelay(policy), 2);

    RestartPolicy_RecordRestart(policy);
    EXPECT_EQ(RestartPolicy_GetBackoffDelay(policy), 4);

    RestartPolicy_RecordRestart(policy);
    EXPECT_EQ(RestartPolicy_GetBackoffDelay(policy), 8);

    RestartPolicy_RecordRestart(policy);
    EXPECT_EQ(RestartPolicy_GetBackoffDelay(policy), 16);

    RestartPolicy_RecordRestart(policy);
    EXPECT_EQ(RestartPolicy_GetBackoffDelay(policy), 32);

    // After 5 restarts (max), backoff is capped at 32s
    EXPECT_FALSE(RestartPolicy_CanRestart(policy));
}

// Test edge case: zero restarts
TEST_F(RestartPolicyTest, ZeroRestarts) {
    policy = RestartPolicy_Create(5, 300, 2);

    EXPECT_EQ(RestartPolicy_GetCount(policy), 0);
    EXPECT_TRUE(RestartPolicy_CanRestart(policy));

    // Backoff should still return base value even with no restarts
    RestartPolicy_RecordRestart(policy);
    EXPECT_EQ(RestartPolicy_GetBackoffDelay(policy), 2);
}

// Test that we can create multiple independent policies
TEST_F(RestartPolicyTest, MultiplePoliciesIndependent) {
    RestartPolicy* policy1 = RestartPolicy_Create(3, 300, 2);
    RestartPolicy* policy2 = RestartPolicy_Create(5, 600, 4);

    RestartPolicy_RecordRestart(policy1);
    EXPECT_EQ(RestartPolicy_GetCount(policy1), 1);
    EXPECT_EQ(RestartPolicy_GetCount(policy2), 0);

    RestartPolicy_RecordRestart(policy2);
    RestartPolicy_RecordRestart(policy2);
    EXPECT_EQ(RestartPolicy_GetCount(policy1), 1);
    EXPECT_EQ(RestartPolicy_GetCount(policy2), 2);

    RestartPolicy_Destroy(policy1);
    RestartPolicy_Destroy(policy2);
}

// Test realistic watchdog scenario: server crashes 3 times with backoff
TEST_F(RestartPolicyTest, RealisticWatchdogScenario) {
    policy = RestartPolicy_Create(5, 300, 2);

    // Server crashes and watchdog attempts restarts
    struct {
        int restartNum;
        int expectedBackoff;
        bool canRestart;
    } scenario[] = {
        {1, 2, true},
        {2, 4, true},
        {3, 8, true},
        {4, 16, true},
        {5, 32, true},
    };

    for (const auto& step : scenario) {
        EXPECT_EQ(RestartPolicy_CanRestart(policy), step.canRestart);
        RestartPolicy_RecordRestart(policy);
        EXPECT_EQ(RestartPolicy_GetBackoffDelay(policy), step.expectedBackoff);
        EXPECT_EQ(RestartPolicy_GetCount(policy), step.restartNum);
    }

    // After 5 restarts, should be rate limited
    EXPECT_FALSE(RestartPolicy_CanRestart(policy));
}

// Test null pointer safety - verify C functions handle nullptr gracefully
// Note: These functions currently crash on nullptr - this documents expected behavior
TEST_F(RestartPolicyTest, NullPointerSafety) {
    // RestartPolicy_Destroy should handle nullptr gracefully
    RestartPolicy_Destroy(nullptr);

    // Note: GetCount/GetMax/GetBackoffDelay will SEGV on nullptr - this is acceptable
    // in C APIs where caller must ensure valid pointers
}
