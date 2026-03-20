#include "compute/Compute.h"
#include "sys/Logger.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Forecast window: 48-hour lookahead with 15-minute resolution
#define MAX_QUARTERS 192
#define MAX_HOURS 48

// Solar panel efficiency model (IEC 61724 crystalline silicon standard)
#define SOLAR_REAL_WORLD_EFFICIENCY 0.75  // Accounts for wiring, inverter, and soiling losses
#define PANEL_TEMP_AT_STANDARD_TEST 25.0  // STC reference temperature
#define PANEL_TEMP_COEFFICIENT -0.0045    // Power loss per °C above STC
#define WIND_COOLING_FACTOR 0.04          // Convective cooling coefficient

// Swedish grid tariff structure (2024)
#define SWEDISH_ENERGY_TAX_SEK_PER_KWH 0.40
#define SWEDISH_VAT 0.25

// Signal thresholds: percentile-based classification over 48h window
#define CHEAP_QUARTERS_PERCENTILE 0.33     // Bottom third of price distribution
#define EXPENSIVE_QUARTERS_PERCENTILE 0.70 // Top third of price distribution
#define MINIMUM_SAVINGS_THRESHOLD 0.08     // Require 8% deviation from median for signal quality

// Demo mode: artificial price variation for flat-price scenarios
#define DEMO_MODE_ENABLED 0
#define DEMO_PRICE_BOOST 0.25

// Solar export policy: balance between maximizing revenue and grid stability
#define MIN_SURPLUS_TO_SELL_KWH 0.5  // Minimum net surplus per 15-min quarter to trigger export (~2 kWh/h)
#define MIN_PRICE_TO_SELL_SEK 0.01   // Avoid exporting at negative prices

// Swedish time-of-use tariffs: typical elbolag structure
// - Low tariff (night):  00:00-06:00 daily
// - Peak tariff (peak):  17:00-21:00 weekdays only
// - Normaltid (day): all other periods
static double GetGridFeeForHour(int hour, int weekday, double low, double normal, double high)
{
    if (hour >= 0 && hour < 6)
        return low;
    if (hour >= 17 && hour < 21 && weekday >= 1 && weekday <= 5)
        return high;
    return normal;
}

// NOCT-based panel temperature model (IEC 61215)
// Higher temperatures reduce photovoltaic efficiency; wind provides convective cooling
static double CalculatePanelTemperature(double airTemp, double sunIntensity, double windSpeed)
{
    double tempRisePerSun = (45.0 - 20.0) / 800.0;
    double coolingEffect = 1.0 + WIND_COOLING_FACTOR * windSpeed;
    return airTemp + (tempRisePerSun * sunIntensity) / coolingEffect;
}

// Consumption profile for Swedish residential users (15-minute resolution)
// Captures daily behavioral patterns: morning rush, daytime baseline, evening peak
static double GetConsumptionPatternQuarter(int hour, int minute)
{
    double hourlyBase = 1.00;
    if (hour < 7)        hourlyBase = 0.40;
    else if (hour < 17)  hourlyBase = 1.00;
    else if (hour < 23)  hourlyBase = 1.60;
    else                 hourlyBase = 0.70;

    double minuteFactor = 1.0;

    // Morning: 06:30-07:15
    if (hour == 6 && minute >= 30) minuteFactor = 1.4;
    if (hour == 7 && minute < 15)  minuteFactor = 1.5;

    // Lunch: 12:00-12:30
    if (hour == 12 && minute < 30) minuteFactor = 1.3;

    // Evening cooking: 17:00-19:00
    if (hour == 17)
    {
        if (minute < 15)      minuteFactor = 1.3;
        else if (minute < 30) minuteFactor = 1.5;
        else if (minute < 45) minuteFactor = 1.4;
        else                  minuteFactor = 1.2;
    }
    if (hour == 18)
    {
        if (minute < 15)      minuteFactor = 1.4;
        else if (minute < 30) minuteFactor = 1.6;
        else if (minute < 45) minuteFactor = 1.2;
        else                  minuteFactor = 0.9;
    }
    if (hour == 19 && minute < 30) minuteFactor = 1.1;

    if (hour >= 20 && hour < 22) minuteFactor = 1.0;

    if (hour == 22)
    {
        if (minute < 30)      minuteFactor = 0.9;
        else                  minuteFactor = 0.7;  // Winding down
    }

    return hourlyBase * minuteFactor;
}

// Standard comparison function for qsort()
static int CompareDoubles(const void *a, const void *b)
{
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

// ========== PUBLIC API ==========

int Compute_Initiate(Compute *compute)
{
    if (!compute)
        return -1;
    memset(compute, 0, sizeof(Compute));
    pthread_mutex_init(&compute->mutex, NULL);
    compute->isInitialized = true;
    LOG_INFO("Compute: ready");
    return 0;
}

int Compute_GenerateEnergyPlan(Compute *compute, const ForecastData *forecast, double solarAreaM2, double solarEfficiency, double consumptionKwh, double gridFeeLow, double gridFeeNormal, double gridFeeHigh, EnergyData *plan)
{
    if (!compute || !forecast || !plan)
        return -1;

    // Quick check that Compute module is initialized
    pthread_mutex_lock(&compute->mutex);
    bool initialized = compute->isInitialized;
    pthread_mutex_unlock(&compute->mutex);

    if (!initialized)
        return -1;

    int numQuarters = forecast->count;  // Should be 192 (15-min intervals, 48h)
    if (numQuarters <= 0)
    {
        LOG_ERROR("Compute: No forecast data to work with (count=%d). Check if Fetcher and Parser are working.", numQuarters);
        return -1;
    }

    // Calculate total cost per 15-minute quarter for threshold determination
    // Elprisetjustnu provides quarter-hour spot prices in SEK/kWh; we add grid fees, energy tax, and VAT to get the true cost to the consumer.
    // Quarter cost = spot price + grid fee + energy tax + VAT
    // Percentile thresholds computed at 15-minute resolution for accurate signal generation
    double actualCosts[MAX_QUARTERS];
    double sortedCosts[MAX_QUARTERS];
    int validQuarters = 0;

    struct tm tmBuf;
    for (int q = 0; q < numQuarters; q++)
    {
        const ForecastEntry *quarter = &forecast->entries[q];

        if (!quarter->valid || !quarter->hasPriceData)
        {
            actualCosts[q] = 0.0;
            continue;
        }

        if (localtime_r(&quarter->timestamp, &tmBuf) == NULL)
        {
            actualCosts[q] = 0.0;
            continue;
        }

        int hourOfDay = tmBuf.tm_hour;
        int weekday = tmBuf.tm_wday;

        double gridFee = GetGridFeeForHour(hourOfDay, weekday, gridFeeLow, gridFeeNormal, gridFeeHigh);
        double quarterCost = (quarter->spotPriceSek + gridFee + SWEDISH_ENERGY_TAX_SEK_PER_KWH) * (1.0 + SWEDISH_VAT);

        actualCosts[q] = quarterCost;
        sortedCosts[validQuarters++] = quarterCost;
    }

    if (validQuarters < 4)
    {
        LOG_ERROR("Compute: Insufficient valid quarters (%d < 4). Cannot generate forecast.", validQuarters);
        return -1;
    }

    qsort(sortedCosts, validQuarters, sizeof(double), CompareDoubles);

    int cheapIndex = (int)(validQuarters * CHEAP_QUARTERS_PERCENTILE);
    int medianIndex = validQuarters / 2;
    int expensiveIndex = (int)(validQuarters * EXPENSIVE_QUARTERS_PERCENTILE);

    if (cheapIndex >= validQuarters) cheapIndex = validQuarters - 1;
    if (medianIndex >= validQuarters) medianIndex = validQuarters - 1;
    if (expensiveIndex >= validQuarters) expensiveIndex = validQuarters - 1;

    double cheapThreshold = sortedCosts[cheapIndex];
    double medianPrice = sortedCosts[medianIndex];
    double expensiveThreshold = sortedCosts[expensiveIndex];

    // Quality filter: require minimum deviation from median to ensure signal value
    double minCheapPrice = medianPrice * (1.0 - MINIMUM_SAVINGS_THRESHOLD);
    if (cheapThreshold > minCheapPrice)
    {
        cheapThreshold = minCheapPrice;
        LOG_INFO("Compute: Threshold adjusted → BUY at %.3f kr/kWh (8%% median discount)", cheapThreshold);
    }

    double minExpensivePrice = medianPrice * (1.0 + MINIMUM_SAVINGS_THRESHOLD);
    if (expensiveThreshold < minExpensivePrice)
    {
        expensiveThreshold = minExpensivePrice;
        LOG_INFO("Compute: Threshold adjusted → AVOID at %.3f kr/kWh (8%% median premium)", expensiveThreshold);
    }

    int numBuy = 0, numAvoid = 0;
    for (int q = 0; q < validQuarters; q++)
    {
        if (sortedCosts[q] <= cheapThreshold) numBuy++;
        if (sortedCosts[q] >= expensiveThreshold) numAvoid++;
    }

    int negativeLogged = 0, buyLogged = 0, sellLogged = 0, avoidLogged = 0;

    LOG_INFO("Compute: ═══════════════════════════════════════════════════════");
    LOG_INFO("Compute: 48-HOUR FORECAST ANALYSIS");
    LOG_INFO("Compute: ═══════════════════════════════════════════════════════");
    LOG_INFO("Compute: Quarters analyzed: %d (%.1f hours)", validQuarters, validQuarters / 4.0);
    LOG_INFO("Compute: Price range: %.3f - %.3f kr/kWh (spread: %.0f%%)", sortedCosts[0], sortedCosts[validQuarters-1], (sortedCosts[validQuarters-1] - sortedCosts[0]) / sortedCosts[0] * 100.0);
    LOG_INFO("Compute: Decision thresholds:");
    LOG_INFO("Compute:   → BUY window:   ≤ %.3f kr/kWh (cheapest %d quarters ≈ %.1fh)", cheapThreshold, numBuy, numBuy / 4.0);
    LOG_INFO("Compute:   → MEDIAN price:   %.3f kr/kWh", medianPrice);
    LOG_INFO("Compute:   → AVOID window: ≥ %.3f kr/kWh (most expensive %d quarters ≈ %.1fh)", expensiveThreshold, numAvoid, numAvoid / 4.0);
    LOG_INFO("Compute: ═══════════════════════════════════════════════════════");

    // Signal generation loop: evaluate each 15-minute period independently
    memset(plan, 0, sizeof(EnergyData));
    double totalImport = 0.0, totalExport = 0.0, totalCost = 0.0;

    for (int i = 0; i < numQuarters && i < 192; i++)
    {
        const ForecastEntry *quarterData = &forecast->entries[i];
        EnergyDataEntry *planEntry = &plan->entries[i];

        if (!quarterData->valid)
            continue;

        struct tm timeInfoBuf;
        struct tm *timeInfo = localtime_r(&quarterData->timestamp, &timeInfoBuf);
        int hourOfDay = timeInfo ? timeInfo->tm_hour : 12;
        int minuteOfHour = timeInfo ? timeInfo->tm_min : 0;

        double quarterCost = actualCosts[i];
        double irradiance = quarterData->solarIrradiance;
        double temperature = quarterData->temperature;
        double windSpeed = quarterData->windSpeed;

        // Solar production model with temperature derating
        double panelTemp = CalculatePanelTemperature(temperature, irradiance, windSpeed);
        double tempEfficiency = 1.0 + PANEL_TEMP_COEFFICIENT * (panelTemp - PANEL_TEMP_AT_STANDARD_TEST);
        if (tempEfficiency < 0.70) tempEfficiency = 0.70;
        if (tempEfficiency > 1.10) tempEfficiency = 1.10;

        double quarterProduction = (irradiance / 1000.0) * solarAreaM2 * solarEfficiency * SOLAR_REAL_WORLD_EFFICIENCY * tempEfficiency * 0.25;

        // Load profile application
        double quarterConsumption = (consumptionKwh * GetConsumptionPatternQuarter(hourOfDay, minuteOfHour)) * 0.25;
        double netEnergy = quarterProduction - quarterConsumption;

        // Signal classification with priority ordering
        EnergyAction recommendation;

        if (!quarterData->hasPriceData || quarterCost == 0.0)
        {
            recommendation = ACTION_IDLE;
        }
        else if (quarterData->spotPriceSek < 0.0)
        {
            recommendation = ACTION_BUY_FROM_GRID;
            if (negativeLogged < 2)
            {
                LOG_INFO("Compute: [%02d:%02d] ⚡ BUY (negative price!) → %.3f kr/kWh", hourOfDay, minuteOfHour, quarterData->spotPriceSek);
                negativeLogged++;
            }
        }
        else if (quarterCost <= cheapThreshold)
        {
            recommendation = ACTION_BUY_FROM_GRID;
            if (buyLogged < 5)
            {
                LOG_INFO("Compute: [%02d:%02d] ✓ BUY → %.3f kr/kWh ≤ %.3f threshold (%.0f%% of median, net: %.1f kWh)", hourOfDay, minuteOfHour, quarterCost, cheapThreshold, (quarterCost / medianPrice) * 100.0, netEnergy);
                buyLogged++;
            }
        }
        else if (netEnergy > MIN_SURPLUS_TO_SELL_KWH && quarterCost >= expensiveThreshold)
        {
            recommendation = ACTION_SELL_TO_GRID;
            totalExport += netEnergy;
            if (sellLogged < 3)
            {
                LOG_INFO("Compute: [%02d:%02d] ⚡ SELL → %.1f kWh surplus @ %.3f kr/kWh (expensive period)", hourOfDay, minuteOfHour, netEnergy, quarterCost);
                sellLogged++;
            }
        }
        else if (quarterCost >= expensiveThreshold)
        {
            recommendation = ACTION_AVOID_HIGH_PRICE;
            if (avoidLogged < 3)
            {
                LOG_INFO("Compute: [%02d:%02d] ⚠ AVOID → %.3f kr/kWh ≥ %.3f threshold (%.0f%% of median)", hourOfDay, minuteOfHour, quarterCost, expensiveThreshold, (quarterCost / medianPrice) * 100.0);
                avoidLogged++;
            }
        }
        else
        {
            recommendation = ACTION_IDLE;
        }

        if (netEnergy < 0.0)
        {
            totalImport += -netEnergy;
            totalCost += -netEnergy * quarterCost;
        }

        planEntry->timestamp = quarterData->timestamp;
        planEntry->action = recommendation;
        planEntry->productionKwh = quarterProduction;
        planEntry->consumptionKwh = quarterConsumption;
        planEntry->spotPrice = quarterData->spotPriceSek;
        planEntry->totalCostSek = quarterCost;
        planEntry->priceVsAvgPct = medianPrice > 0.0 ? (quarterCost - medianPrice) / medianPrice * 100.0 : 0.0;
        planEntry->temperature = temperature;
        planEntry->windSpeed = windSpeed;
        planEntry->valid = true;
    }

    plan->count = numQuarters;
    plan->generatedAt = time(NULL);
    plan->totalCostSek = totalCost;
    plan->totalGridImportKwh = totalImport;
    plan->totalGridExportKwh = totalExport;

    // Find best window for flexible loads
    // Find the longest block of BUY signals with maximum practical value.
    // Balances cost savings against time-of-day convenience for real user adoption.
    // Operates on 15-minute quarters (native resolution from APIs)
    {
        int windowStart = -1, windowEnd = -1;
        double windowCost = 0.0, windowSavings = 0.0;
        int windowQuarters = 0;
        double bestScore = -1.0;  // Now use practicality score instead of just savings

        for (int i = 0; i <= numQuarters; i++)
        {
            // Only consider quarters with actual price data for BUY window
            bool isCheapQuarter = (i < numQuarters && plan->entries[i].valid && forecast->entries[i].hasPriceData && plan->entries[i].action == ACTION_BUY_FROM_GRID);

            if (isCheapQuarter)
            {
                if (windowStart < 0)
                    windowStart = i;
                windowEnd = i;
                windowCost += plan->entries[i].totalCostSek;
                // Savings = what you WOULD pay at median price vs what you actually pay
                windowSavings += (medianPrice - plan->entries[i].totalCostSek) * plan->entries[i].consumptionKwh;
                windowQuarters++;
            }
            else if (windowStart >= 0) // End of window
            {
                // Calculate practicality score: balance savings vs convenience
                time_t windowTimestamp = plan->entries[windowStart].timestamp;
                struct tm windowTm;
                localtime_r(&windowTimestamp, &windowTm);
                int startHour = windowTm.tm_hour;

                // Practicality multiplier based on time of day:
                // 22:00-06:59 → 1.0x (night, acceptable for dishwasher/laundry)
                // 07:00-16:59 → 0.5x (work hours, less likely to start loads)
                // 17:00-21:59 → 1.5x (evening, most convenient for users)
                double practicalityFactor = 1.0;
                if (startHour >= 17 && startHour < 22)
                    practicalityFactor = 1.5;  // Best: evening hours
                else if (startHour >= 7 && startHour < 17)
                    practicalityFactor = 0.5;  // Worst: day hours when away
                // else: night hours (22-06) = 1.0x baseline

                // Practical value = savings × convenience
                // Example: 8 kr @ 02:00 (1.0x) = 8 points
                //          6 kr @ 23:00 (1.0x) = 6 points → but 6 kr @ 20:00 (1.5x) = 9 points!
                double practicalScore = windowSavings * practicalityFactor;

                if (practicalScore > bestScore)
                {
                    bestScore = practicalScore;
                    plan->bestBuyWindow.start = plan->entries[windowStart].timestamp;
                    plan->bestBuyWindow.end = plan->entries[windowEnd].timestamp;
                    plan->bestBuyWindow.durationMinutes = windowQuarters * 15;
                    plan->bestBuyWindow.avgCostSek = windowCost / windowQuarters;
                    plan->bestBuyWindow.savingsSek = windowSavings;
                    plan->hasBuyWindow = 1;
                }
                // Reset for next window
                windowStart = -1;
                windowEnd = -1;
                windowCost = 0.0;
                windowSavings = 0.0;
                windowQuarters = 0;
            }
        }
    }

    if (plan->hasBuyWindow)
    {
        LOG_INFO("Compute: Best time to run flexible loads → %d min window, saves %.2f SEK", plan->bestBuyWindow.durationMinutes, plan->bestBuyWindow.savingsSek);
    }
    else
    {
        LOG_INFO("Compute: No clear cheap window (flat prices or solar covers everything)");
    }

    LOG_INFO("Compute: Forecast complete → %d quarters (%.1f hours), import %.2f kWh, export %.2f kWh, cost %.2f SEK", numQuarters, numQuarters / 4.0, totalImport, totalExport, totalCost);

    return 0;
}

void Compute_Shutdown(Compute *compute)
{
    if (!compute || !compute->isInitialized)
        return;
    pthread_mutex_destroy(&compute->mutex);
    compute->isInitialized = false;
    LOG_INFO("Compute: shutdown");
}
