// Compute Energy Plan Tests
// Verifies signal generation (BUY/SELL/AVOID/IDLE) under controlled price/solar conditions.
//
// Design principle: all grid fees are set to 0 so that total cost depends only on
// spot price + fixed Swedish energy tax (0.40 kr/kWh) + VAT (1.25x).
// This makes threshold calculations deterministic regardless of time-of-day.
//
// Cost formula: (spot + 0.40) * 1.25
//   spot 0.2 → 0.750 kr/kWh
//   spot 1.5 → 2.375 kr/kWh
//   spot 3.5 → 4.875 kr/kWh

#include <gtest/gtest.h>
#include <cstring>
#include <ctime>

extern "C" {
#include "compute/Compute.h"
#include "domain/Forecast.h"
#include "domain/Energy.h"
}

// Base time: 2023-01-01 00:00:00 UTC (Sunday, low-tariff hour)
static const time_t BASE_TIME = 1672531200;

class ComputeTest : public ::testing::Test {
protected:
    Compute compute;

    void SetUp() override {
        ASSERT_EQ(Compute_Initiate(&compute), 0);
    }
    void TearDown() override {
        Compute_Shutdown(&compute);
    }

    // Build a 96-entry ForecastData with three price bands.
    // irradiances[0..2] controls solar production per band.
    ForecastData MakeForecast(double spot0, double spot1, double spot2,
                              double irr0, double irr1, double irr2,
                              double temp = 20.0, double wind = 2.0) {
        ForecastData f = {};
        f.count = 96;
        for (int i = 0; i < 96; i++) {
            ForecastEntry &e = f.entries[i];
            e.timestamp     = BASE_TIME + (time_t)(i * 900);
            e.valid         = true;
            e.hasPriceData  = true;
            e.temperature   = temp;
            e.windSpeed     = wind;
            e.humidity      = 50.0;
            e.cloudCover    = 0.0;

            if      (i < 32) { e.spotPriceSek = spot0; e.solarIrradiance = irr0; }
            else if (i < 64) { e.spotPriceSek = spot1; e.solarIrradiance = irr1; }
            else             { e.spotPriceSek = spot2; e.solarIrradiance = irr2; }
        }
        f.lastUpdated = BASE_TIME;
        return f;
    }
};

// Plan count must match the number of forecast entries
TEST_F(ComputeTest, PlanCountMatchesForecastCount) {
    ForecastData f = MakeForecast(0.2, 1.5, 3.5, 0, 0, 0);
    EnergyData plan = {};
    ASSERT_EQ(Compute_GenerateEnergyPlan(&compute, &f, 0, 0, 1.0, 0, 0, 0, &plan), 0);
    EXPECT_EQ(plan.count, 96);
}

// Cheap entries (spot 0.2) must receive BUY signals; expensive entries (spot 3.5) AVOID
// Medium entries (spot 1.5) fall between the thresholds → IDLE
TEST_F(ComputeTest, BuyAndAvoidSignalsWithThreePriceLevels) {
    // 32 cheap / 32 medium / 32 expensive, no solar → no SELL
    ForecastData f = MakeForecast(0.2, 1.5, 3.5, 0, 0, 0);
    EnergyData plan = {};
    ASSERT_EQ(Compute_GenerateEnergyPlan(&compute, &f, 0, 0, 0.5, 0, 0, 0, &plan), 0);

    int buy = 0, idle = 0, avoid = 0, sell = 0;
    for (int i = 0; i < plan.count; i++) {
        if (!plan.entries[i].valid) continue;
        switch (plan.entries[i].action) {
            case ACTION_BUY_FROM_GRID:    buy++;  break;
            case ACTION_IDLE:             idle++; break;
            case ACTION_AVOID_HIGH_PRICE: avoid++;break;
            case ACTION_SELL_TO_GRID:     sell++; break;
        }
    }
    EXPECT_EQ(buy,  32); // spot 0.2 → cost 0.750 ≤ cheap_threshold (0.750)
    EXPECT_EQ(idle, 32); // spot 1.5 → cost 2.375, between thresholds
    EXPECT_EQ(avoid,32); // spot 3.5 → cost 4.875 ≥ expensive_threshold (4.875)
    EXPECT_EQ(sell,  0);
}

// With large solar panels + high irradiance during expensive periods, SELL must be emitted
// instead of AVOID (net surplus > 0.5 kWh per 15-min quarter)
TEST_F(ComputeTest, SellSignalsWhenSolarSurplusAndExpensivePrice) {
    // Cheap band: no solar; Medium band: no solar; Expensive band: 1000 W/m² irradiance
    ForecastData f = MakeForecast(0.2, 1.5, 3.5, 0, 0, 1000);
    EnergyData plan = {};
    // 50 m² panels at 20% efficiency → ≈1.68 kWh/quarter, consumption ≈0.1 kWh → net >>0.5
    ASSERT_EQ(Compute_GenerateEnergyPlan(&compute, &f, 50.0, 0.20, 0.5, 0, 0, 0, &plan), 0);

    int sell = 0, avoid = 0;
    for (int i = 64; i < 96; i++) { // expensive band
        if (!plan.entries[i].valid) continue;
        if (plan.entries[i].action == ACTION_SELL_TO_GRID)     sell++;
        if (plan.entries[i].action == ACTION_AVOID_HIGH_PRICE) avoid++;
    }
    EXPECT_EQ(sell,  32); // all expensive entries have enough solar surplus → SELL
    EXPECT_EQ(avoid,  0);
}

// Negative spot price always triggers BUY regardless of thresholds
TEST_F(ComputeTest, NegativePriceAlwaysBuy) {
    ForecastData f = {};
    f.count = 96;
    for (int i = 0; i < 96; i++) {
        f.entries[i].timestamp    = BASE_TIME + (time_t)(i * 900);
        f.entries[i].valid        = true;
        f.entries[i].hasPriceData = true;
        f.entries[i].spotPriceSek = (i < 10) ? -0.5 : 1.5; // first 10 negative
        f.entries[i].temperature  = 15.0;
        f.entries[i].windSpeed    = 2.0;
    }
    f.lastUpdated = BASE_TIME;

    EnergyData plan = {};
    ASSERT_EQ(Compute_GenerateEnergyPlan(&compute, &f, 0, 0, 0.5, 0, 0, 0, &plan), 0);

    for (int i = 0; i < 10; i++) {
        EXPECT_EQ(plan.entries[i].action, ACTION_BUY_FROM_GRID)
            << "Entry " << i << " with negative price must be BUY";
    }
}

// Total cost must be positive when forecast has positive prices and consumption
TEST_F(ComputeTest, TotalCostIsPositive) {
    ForecastData f = MakeForecast(1.0, 1.5, 2.0, 0, 0, 0);
    EnergyData plan = {};
    ASSERT_EQ(Compute_GenerateEnergyPlan(&compute, &f, 0, 0, 1.0, 0, 0, 0, &plan), 0);
    EXPECT_GT(plan.totalCostSek, 0.0);
    EXPECT_EQ(plan.count, 96);
}

// Null pointers for any argument must return -1
TEST_F(ComputeTest, NullPointerSafety) {
    ForecastData f = MakeForecast(1.0, 1.5, 2.0, 0, 0, 0);
    EnergyData plan = {};
    EXPECT_EQ(Compute_GenerateEnergyPlan(nullptr,   &f,    0, 0, 1.0, 0, 0, 0, &plan), -1);
    EXPECT_EQ(Compute_GenerateEnergyPlan(&compute,  nullptr, 0, 0, 1.0, 0, 0, 0, &plan), -1);
    EXPECT_EQ(Compute_GenerateEnergyPlan(&compute,  &f,    0, 0, 1.0, 0, 0, 0, nullptr), -1);
}

// Uninitialized Compute struct must fail gracefully
TEST_F(ComputeTest, UninitializedComputeReturnsError) {
    Compute uninit = {};
    ForecastData f = MakeForecast(1.0, 1.5, 2.0, 0, 0, 0);
    EnergyData plan = {};
    EXPECT_EQ(Compute_GenerateEnergyPlan(&uninit, &f, 0, 0, 1.0, 0, 0, 0, &plan), -1);
}

// A forecast with count=0 has no valid quarters → error
TEST_F(ComputeTest, EmptyForecastReturnsError) {
    ForecastData f = {};
    f.count = 0;
    EnergyData plan = {};
    EXPECT_EQ(Compute_GenerateEnergyPlan(&compute, &f, 0, 0, 1.0, 0, 0, 0, &plan), -1);
}

// Fewer than 4 valid quarters must fail
TEST_F(ComputeTest, InsufficientValidQuartersReturnsError) {
    ForecastData f = {};
    f.count = 3;
    for (int i = 0; i < 3; i++) {
        f.entries[i].timestamp    = BASE_TIME + (time_t)(i * 900);
        f.entries[i].valid        = true;
        f.entries[i].hasPriceData = true;
        f.entries[i].spotPriceSek = 1.0;
        f.entries[i].temperature  = 20.0;
        f.entries[i].windSpeed    = 2.0;
    }
    f.lastUpdated = BASE_TIME;

    EnergyData plan = {};
    EXPECT_EQ(Compute_GenerateEnergyPlan(&compute, &f, 0, 0, 1.0, 0, 0, 0, &plan), -1);
}
