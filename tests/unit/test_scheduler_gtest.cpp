// GridGuard Energy-Aware Scheduling Tests
// Tests spot price optimization - the core domain logic that makes GridGuard unique

#include <gtest/gtest.h>
#include <ctime>
#include <vector>

extern "C" {
    #include "compute/LoadScheduler.h"
}

class LoadSchedulerTest : public ::testing::Test {
protected:
    time_t baseTime;

    void SetUp() override {
        // Use a fixed base time for reproducible tests
        baseTime = 1672531200; // 2023-01-01 00:00:00 UTC
    }

    // Helper to create price entries.
    // Each slot is 15 minutes (900 s) — the scheduler's native granularity.
    // productionKwh is explicitly zeroed so tests focus on price optimisation only.
    std::vector<SchedulerEntry> CreatePriceSlots(const std::vector<double>& prices) {
        std::vector<SchedulerEntry> entries;
        for (size_t i = 0; i < prices.size(); i++) {
            SchedulerEntry entry = {};
            entry.timestamp = baseTime + (i * 900); // Each slot is 15 minutes
            entry.totalCostPerKwh = prices[i];
            entry.productionKwh = 0.0;
            entries.push_back(entry);
        }
        return entries;
    }
};

// Test basic scheduling: find cheapest 1-hour window.
// The scheduler sums 4 consecutive 15-minute quarters per window (4 * 15 = 60 min).
// Data is designed so the cheapest 4-entry window starts at index 2.
TEST_F(LoadSchedulerTest, FindCheapestSingleHourWindow) {
    // 8 entries at 15-min intervals; cheap block at indices 2-5
    auto entries = CreatePriceSlots({3.0, 2.5, 0.5, 0.5, 0.5, 0.5, 2.5, 3.0});

    ScheduleWindow result;
    int status = LoadScheduler_FindWindow(
        entries.data(), entries.size(),
        60,    // 60 minutes = 4 quarters
        2.0,   // 2 kW load
        0,     // no deadline
        baseTime,
        &result
    );

    ASSERT_EQ(status, 0);

    // Window at index 2 (baseTime + 2*900) is cheapest:
    // 4 * (0.5 SEK/kWh * 2 kW * 0.25 h) = 1.0 SEK
    EXPECT_EQ(result.scheduledStart, baseTime + (2 * 900));
    EXPECT_EQ(result.durationMinutes, 60);
    EXPECT_EQ(result.powerKw, 2.0);
    EXPECT_NEAR(result.estimatedCostSek, 1.0, 0.01);
}

// Test multi-hour scheduling: dishwasher running for 2 hours.
// 120 min = 8 quarters. Data has cheap block starting at index 4.
TEST_F(LoadSchedulerTest, FindCheapestMultiHourWindow) {
    // 12 entries; cheap block at indices 4-11
    auto entries = CreatePriceSlots({
        4.0, 3.5, 3.0, 2.5,
        0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5
    });

    ScheduleWindow result;
    int status = LoadScheduler_FindWindow(
        entries.data(), entries.size(),
        120,   // 2 hours = 8 quarters
        1.5,   // 1.5 kW (dishwasher)
        0,
        baseTime,
        &result
    );

    ASSERT_EQ(status, 0);

    // Window at index 4 (baseTime + 4*900) is cheapest:
    // 8 * (0.5 SEK/kWh * 1.5 kW * 0.25 h) = 1.5 SEK
    EXPECT_EQ(result.scheduledStart, baseTime + (4 * 900));
    EXPECT_NEAR(result.estimatedCostSek, 1.5, 0.01);
}

// Test scheduling with deadline constraint.
// 60 min = 4 quarters. Deadline cuts off later windows.
TEST_F(LoadSchedulerTest, RespectDeadlineConstraint) {
    auto entries = CreatePriceSlots({3.0, 2.5, 1.0, 2.0, 1.5, 2.5});

    ScheduleWindow result;
    // Deadline: 6 quarters (90 min) from baseTime
    time_t deadline = baseTime + (6 * 900);

    int status = LoadScheduler_FindWindow(
        entries.data(), entries.size(),
        60,
        2.0,
        deadline,
        baseTime,
        &result
    );

    ASSERT_EQ(status, 0);

    // All valid windows (i=0,1,2) end before or at the deadline.
    // The cheapest valid window must finish no later than the deadline.
    time_t windowEnd = result.scheduledStart + (result.durationMinutes * 60);
    EXPECT_LE(windowEnd, deadline);
}

// Test partial hour scheduling: 30-minute load.
// 30 min = 2 quarters. Window at index 1 is cheapest.
TEST_F(LoadSchedulerTest, PartialHourScheduling) {
    // Cheap block at indices 1-2
    auto entries = CreatePriceSlots({3.0, 0.5, 0.5});

    ScheduleWindow result;
    int status = LoadScheduler_FindWindow(
        entries.data(), entries.size(),
        30,    // 30 minutes = 2 quarters
        2.0,
        0,
        baseTime,
        &result
    );

    ASSERT_EQ(status, 0);

    // Window at index 1 (baseTime + 900) is cheapest:
    // 2 * (0.5 SEK/kWh * 2 kW * 0.25 h) = 0.5 SEK
    EXPECT_EQ(result.scheduledStart, baseTime + 900);
    EXPECT_NEAR(result.estimatedCostSek, 0.5, 0.01);
}

// Test savings calculation: compare to immediate start.
// 60 min = 4 quarters. Two windows exist; second is cheaper.
TEST_F(LoadSchedulerTest, CalculateSavingsVsImmediateStart) {
    // 5 entries: first window has a high price at index 0,
    // second window (indices 1-4) is uniformly cheap.
    auto entries = CreatePriceSlots({4.0, 1.0, 1.0, 1.0, 1.0});

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

    // nowCost  (window i=0): (4.0+1.0+1.0+1.0) * 1.0 * 0.25 = 1.75 SEK
    // bestCost (window i=1): (1.0+1.0+1.0+1.0) * 1.0 * 0.25 = 1.00 SEK
    // savings = 1.75 - 1.00 = 0.75 SEK
    EXPECT_NEAR(result.savingsSek, 0.75, 0.01);
}

// Test that scheduler skips slots whose timestamp is before nowTime.
// 60 min = 4 quarters. Entries 0-1 are in the past; first valid window starts at index 2.
TEST_F(LoadSchedulerTest, SkipPastTimeSlots) {
    // 8 entries at 15-min intervals; nowTime = baseTime + 2*900
    auto entries = CreatePriceSlots({1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0, 4.5});

    time_t nowTime = baseTime + (2 * 900); // Current time = start of entry 2

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

    // Entries 0 and 1 are in the past; scheduler must pick index 2 or later
    EXPECT_GE(result.scheduledStart, nowTime);
}

// Test realistic EV charging scenario: 8 hours overnight.
// 480 min = 32 quarters. Uses 96 entries (24 h at 15-min granularity).
TEST_F(LoadSchedulerTest, ElectricVehicleChargingScenario) {
    // 96 entries covering 24 hours: cheap night (indices 0-31),
    // morning peak (32-47), day rates (48-95)
    std::vector<double> prices;
    for (int i = 0; i < 96; i++) {
        if      (i < 32) prices.push_back(0.9);  // Hours 0-7: cheap night
        else if (i < 48) prices.push_back(3.5);  // Hours 8-11: morning peak
        else             prices.push_back(2.0);  // Hours 12-23: day
    }
    auto entries = CreatePriceSlots(prices);

    ScheduleWindow result;
    int status = LoadScheduler_FindWindow(
        entries.data(), entries.size(),
        480,   // 8 hours = 32 quarters
        3.5,   // 3.5 kW (home charger)
        baseTime + (12 * 3600), // Must finish by noon
        baseTime,
        &result
    );

    ASSERT_EQ(status, 0);

    // Cheapest 32-quarter window sits entirely within the cheap night block (index 0)
    EXPECT_LT(result.scheduledStart, baseTime + (8 * 3600));
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
    // 7 entries: window at index 1 (entries 1-4) and window at index 2 (entries 2-5)
    // both sum to 4 * 1.0 = 4.0. Scheduler should pick the earlier one (index 1).
    auto entries = CreatePriceSlots({3.0, 1.0, 1.0, 1.0, 1.0, 1.0, 3.0});

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

    // Both window at index 1 and index 2 are equally cheap; prefer earlier (index 1)
    EXPECT_EQ(result.scheduledStart, baseTime + (1 * 900));
}

// Test that the practicality score causes a slightly more expensive evening window
// to be preferred over a cheaper daytime window.
//
// Timestamps are placed at 12:00 UTC (day, factor 0.5) and 17:00 UTC (evening,
// factor 1.5). This holds for UTC and CET/CEST (UTC+1/+2) — the CI runner and
// Swedish development environments.
//
// Day window     (12:00 UTC): cost = 1.00 SEK, factor 0.5, weighted = 2.00
// Evening window (17:00 UTC): cost = 1.20 SEK, factor 1.5, weighted = 0.80
//
// Without practicality: day wins (1.00 < 1.20)
// With practicality:    evening wins (0.80 < 2.00)
TEST_F(LoadSchedulerTest, PracticalityScorePrefersEveningOverCheaperDay) {
    std::vector<SchedulerEntry> entries;

    // Day block: 12:00–12:45 UTC, price 1.0 kr/kWh
    time_t dayStart = baseTime + 12 * 3600;
    for (int i = 0; i < 4; i++) {
        SchedulerEntry e = {};
        e.timestamp       = dayStart + i * 900;
        e.totalCostPerKwh = 1.0;
        e.productionKwh   = 0.0;
        entries.push_back(e);
    }

    // Evening block: 17:00–17:45 UTC, price 1.2 kr/kWh
    time_t eveningStart = baseTime + 17 * 3600;
    for (int i = 0; i < 4; i++) {
        SchedulerEntry e = {};
        e.timestamp       = eveningStart + i * 900;
        e.totalCostPerKwh = 1.2;
        e.productionKwh   = 0.0;
        entries.push_back(e);
    }

    ScheduleWindow result;
    int status = LoadScheduler_FindWindow(
        entries.data(), (int)entries.size(),
        60,       // 60 min = 4 quarters
        1.0,      // 1 kW load
        0,        // no deadline
        baseTime, // nowTime before all entries
        &result
    );

    ASSERT_EQ(status, 0);

    // Evening window should win due to practicality (weighted 0.80 < 2.00).
    EXPECT_EQ(result.scheduledStart, eveningStart);

    // estimatedCostSek must reflect actual cost, not weighted cost.
    // 4 quarters × 1.2 kr/kWh × 1.0 kW × 0.25 h = 1.20 SEK
    EXPECT_NEAR(result.estimatedCostSek, 1.20, 0.01);
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
