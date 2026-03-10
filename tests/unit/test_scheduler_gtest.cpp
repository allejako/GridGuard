// GridGuard Energy-Aware Scheduling Tests
// Tests spot price optimization - the core domain logic that makes GridGuard unique

#include <gtest/gtest.h>
#include <ctime>
#include <vector>

extern "C" {
    #include "domain/Scheduler.h"
}

class LoadSchedulerTest : public ::testing::Test {
protected:
    time_t baseTime;

    void SetUp() override {
        // Use a fixed base time for reproducible tests
        baseTime = 1672531200; // 2023-01-01 00:00:00 UTC
    }

    // Helper to create price entries
    std::vector<SchedulerEntry> CreatePriceSlots(const std::vector<double>& prices) {
        std::vector<SchedulerEntry> entries;
        for (size_t i = 0; i < prices.size(); i++) {
            SchedulerEntry entry;
            entry.timestamp = baseTime + (i * 3600); // Each slot is 1 hour
            entry.totalCostPerKwh = prices[i];
            entries.push_back(entry);
        }
        return entries;
    }
};

// Test basic scheduling: find cheapest 1-hour window
TEST_F(LoadSchedulerTest, FindCheapestSingleHourWindow) {
    // Spot prices varying throughout the day (SEK/kWh)
    auto entries = CreatePriceSlots({2.5, 1.8, 1.2, 2.0, 3.5, 2.2});

    ScheduleWindow result;
    int status = LoadScheduler_FindWindow(
        entries.data(), entries.size(),
        60,    // 60 minutes
        2.0,   // 2 kW load (e.g., washing machine)
        0,     // no deadline
        baseTime,
        &result
    );

    ASSERT_EQ(status, 0);

    // Should pick hour 2 (index 2) with price 1.2 SEK/kWh
    EXPECT_EQ(result.scheduledStart, baseTime + (2 * 3600));
    EXPECT_EQ(result.durationMinutes, 60);
    EXPECT_EQ(result.powerKw, 2.0);

    // Cost should be 1.2 SEK/kWh * 2 kW * 1 hour = 2.4 SEK
    EXPECT_NEAR(result.estimatedCostSek, 2.4, 0.01);
}

// Test multi-hour scheduling: dishwasher running for 2 hours
TEST_F(LoadSchedulerTest, FindCheapestMultiHourWindow) {
    // 24-hour spot price profile
    auto entries = CreatePriceSlots({
        3.0, 2.8, 2.5, 2.0, 1.5, 1.2,  // 00:00-05:59 (night - cheap)
        1.8, 2.5, 3.5, 4.0, 3.8, 3.2,  // 06:00-11:59 (morning peak)
        2.8, 2.5, 2.2, 2.0, 1.8, 2.0,  // 12:00-17:59 (afternoon)
        2.5, 3.0, 3.5, 3.2, 2.8, 2.5   // 18:00-23:59 (evening peak)
    });

    ScheduleWindow result;
    int status = LoadScheduler_FindWindow(
        entries.data(), entries.size(),
        120,   // 2 hours
        1.5,   // 1.5 kW (dishwasher)
        0,
        baseTime,
        &result
    );

    ASSERT_EQ(status, 0);

    // Should schedule at hours 4-5 (indices 4-5: 1.5 + 1.2 = 2.7 SEK/kWh)
    EXPECT_EQ(result.scheduledStart, baseTime + (4 * 3600));

    // Cost: (1.5 + 1.2) * 1.5 kW * 1 hour/slot = 4.05 SEK
    EXPECT_NEAR(result.estimatedCostSek, 4.05, 0.01);
}

// Test scheduling with deadline constraint
TEST_F(LoadSchedulerTest, RespectDeadlineConstraint) {
    auto entries = CreatePriceSlots({3.0, 2.5, 1.0, 2.0, 1.5, 2.5});

    ScheduleWindow result;
    time_t deadline = baseTime + (3 * 3600); // Must finish by hour 3

    int status = LoadScheduler_FindWindow(
        entries.data(), entries.size(),
        60,
        2.0,
        deadline,
        baseTime,
        &result
    );

    ASSERT_EQ(status, 0);

    // Window finishes at start + duration
    time_t windowEnd = result.scheduledStart + (result.durationMinutes * 60);

    // Should NOT pick hour 2 (cheapest at 1.0) because it would finish after deadline
    // Should pick hour 0 or 1 instead
    EXPECT_LE(windowEnd, deadline);
}

// Test partial hour scheduling: 30-minute load
TEST_F(LoadSchedulerTest, PartialHourScheduling) {
    auto entries = CreatePriceSlots({2.0, 1.5, 3.0});

    ScheduleWindow result;
    int status = LoadScheduler_FindWindow(
        entries.data(), entries.size(),
        30,    // 30 minutes
        2.0,
        0,
        baseTime,
        &result
    );

    ASSERT_EQ(status, 0);

    // Should pick hour 1 (1.5 SEK/kWh)
    EXPECT_EQ(result.scheduledStart, baseTime + 3600);

    // Cost: 1.5 SEK/kWh * 2 kW * 0.5 hour = 1.5 SEK
    EXPECT_NEAR(result.estimatedCostSek, 1.5, 0.01);
}

// Test savings calculation: compare to immediate start
TEST_F(LoadSchedulerTest, CalculateSavingsVsImmediateStart) {
    auto entries = CreatePriceSlots({3.0, 2.5, 1.0, 2.0});

    ScheduleWindow result;
    int status = LoadScheduler_FindWindow(
        entries.data(), entries.size(),
        60,
        1.0,
        0,
        baseTime,
        &result
    );

    ASSERT_EQ(status, 0);

    // Immediate start cost: 3.0 * 1.0 * 1 = 3.0 SEK
    // Optimal start (hour 2): 1.0 * 1.0 * 1 = 1.0 SEK
    // Savings: 3.0 - 1.0 = 2.0 SEK
    EXPECT_NEAR(result.savingsSek, 2.0, 0.01);
}

// Test that scheduler skips past slots
TEST_F(LoadSchedulerTest, SkipPastTimeSlots) {
    auto entries = CreatePriceSlots({1.0, 1.5, 2.0, 2.5});

    time_t nowTime = baseTime + (2 * 3600); // Current time is hour 2

    ScheduleWindow result;
    int status = LoadScheduler_FindWindow(
        entries.data(), entries.size(),
        60,
        1.0,
        0,
        nowTime,
        &result
    );

    ASSERT_EQ(status, 0);

    // Should NOT pick hours 0 or 1 (in the past)
    // Should pick hour 2 or later
    EXPECT_GE(result.scheduledStart, nowTime);
}

// Test realistic EV charging scenario: 8 hours overnight
TEST_F(LoadSchedulerTest, ElectricVehicleChargingScenario) {
    // Full day prices with cheap night rates
    auto entries = CreatePriceSlots({
        2.0, 1.8, 1.5, 1.2, 1.0, 0.9,  // Night (cheap)
        1.2, 2.0, 3.5, 4.0, 3.8, 3.0,  // Morning peak
        2.5, 2.2, 2.0, 2.5, 3.0, 3.5,  // Afternoon/Evening
        4.0, 3.8, 3.2, 2.8, 2.5, 2.2   // Evening peak
    });

    ScheduleWindow result;
    int status = LoadScheduler_FindWindow(
        entries.data(), entries.size(),
        480,   // 8 hours (overnight EV charging)
        3.5,   // 3.5 kW (home charger)
        baseTime + (12 * 3600), // Must be done by noon
        baseTime,
        &result
    );

    ASSERT_EQ(status, 0);

    // Should schedule during night hours (0-7) for lowest cost
    EXPECT_LT(result.scheduledStart, baseTime + (8 * 3600));

    // Verify cost is calculated correctly for 8-hour window
    EXPECT_GT(result.estimatedCostSek, 0);
}

// Test error handling: no valid window exists
TEST_F(LoadSchedulerTest, NoValidWindowExists) {
    auto entries = CreatePriceSlots({2.0, 2.5, 3.0});

    ScheduleWindow result;
    time_t tightDeadline = baseTime + 1800; // Deadline in 30 minutes

    int status = LoadScheduler_FindWindow(
        entries.data(), entries.size(),
        120,   // Need 2 hours
        1.0,
        tightDeadline, // But deadline is too soon
        baseTime,
        &result
    );

    // Should fail - no window fits constraints
    EXPECT_EQ(status, -1);
}

// Test null pointer safety
TEST_F(LoadSchedulerTest, NullPointerHandling) {
    auto entries = CreatePriceSlots({2.0, 2.5});
    ScheduleWindow result;

    EXPECT_EQ(LoadScheduler_FindWindow(nullptr, 2, 60, 1.0, 0, baseTime, &result), -1);
    EXPECT_EQ(LoadScheduler_FindWindow(entries.data(), 2, 60, 1.0, 0, baseTime, nullptr), -1);
}

// Test invalid parameters
TEST_F(LoadSchedulerTest, InvalidParameters) {
    auto entries = CreatePriceSlots({2.0, 2.5});
    ScheduleWindow result;

    // Zero duration
    EXPECT_EQ(LoadScheduler_FindWindow(entries.data(), 2, 0, 1.0, 0, baseTime, &result), -1);

    // Zero power
    EXPECT_EQ(LoadScheduler_FindWindow(entries.data(), 2, 60, 0.0, 0, baseTime, &result), -1);

    // Negative power
    EXPECT_EQ(LoadScheduler_FindWindow(entries.data(), 2, 60, -1.0, 0, baseTime, &result), -1);
}

// Test that scheduler prefers earlier slot when prices are equal
TEST_F(LoadSchedulerTest, PrefersEarlierWhenPricesEqual) {
    auto entries = CreatePriceSlots({2.0, 1.5, 1.5, 2.5});

    ScheduleWindow result;
    int status = LoadScheduler_FindWindow(
        entries.data(), entries.size(),
        60,
        1.0,
        0,
        baseTime,
        &result
    );

    ASSERT_EQ(status, 0);

    // When hours 1 and 2 have same price, should prefer hour 1 (earlier)
    EXPECT_EQ(result.scheduledStart, baseTime + 3600);
}

// Test washing machine scenario: 90-minute cycle during cheap hours
TEST_F(LoadSchedulerTest, WashingMachine90MinuteCycle) {
    auto entries = CreatePriceSlots({3.5, 3.0, 2.0, 1.5, 1.2, 2.5, 3.0});

    ScheduleWindow result;
    int status = LoadScheduler_FindWindow(
        entries.data(), entries.size(),
        90,    // 1.5 hours
        0.8,   // 800W washing machine
        0,
        baseTime,
        &result
    );

    ASSERT_EQ(status, 0);

    // Should find optimal 90-minute window
    EXPECT_EQ(result.durationMinutes, 90);
    EXPECT_NEAR(result.powerKw, 0.8, 0.01);
}
