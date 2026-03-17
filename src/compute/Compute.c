#include "compute/Compute.h"
#include "sys/Logger.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ========== FORECAST CONSTANTS ==========
#define MAX_QUARTERS 192  // 48 hours × 4 quarters per hour
#define MAX_HOURS 48      // 48-hour forecast window

// ========== SOLAR PANEL CONSTANTS ==========
// These are based on IEC 61724 standards for crystalline silicon panels.
// Real-world solar panels lose ~25% efficiency from ideal lab conditions.
#define SOLAR_REAL_WORLD_EFFICIENCY 0.75 // Wiring, inverter, dust losses

// Temperature affects solar panels significantly:
// - Hot panels (summer) produce LESS power than cold panels (winter)
// - Wind cools panels down, improving performance
#define PANEL_TEMP_AT_STANDARD_TEST 25.0 // °C - lab test condition
#define PANEL_TEMP_COEFFICIENT -0.0045   // -0.45% per °C above 25°C
#define WIND_COOLING_FACTOR 0.04         // More wind = cooler panels

// ========== SWEDISH ELECTRICITY COSTS ==========
// These are fixed costs that EVERY Swedish household pays, regardless of spot price.
#define SWEDISH_ENERGY_TAX_SEK_PER_KWH 0.40 // Energiskatt (2024)
#define SWEDISH_VAT 0.25                    // 25% moms on everything

// ========== DECISION THRESHOLDS ==========
// Production-ready percentile-based decision algorithm.
// Uses quarter-hour granularity (192 periods over 48h) for accurate signals.
#define CHEAP_QUARTERS_PERCENTILE 0.33   // Bottom 33% = cheapest 64 quarters = ~16 hours
#define EXPENSIVE_QUARTERS_PERCENTILE 0.70 // Top 30% = most expensive periods

// Quality check: Ensure meaningful price difference (at least 8% savings)
// Lower threshold = more BUY signals but less savings guarantee
#define MINIMUM_SAVINGS_THRESHOLD 0.08 // 8% discount required for real customer value

// ========== DEMO MODE ==========
// On flat-price days, add artificial variation to make demo more interesting.
// This multiplier is applied based on time-of-day to simulate typical patterns.
#define DEMO_MODE_ENABLED 0     // Set to 0 to disable demo boost
#define DEMO_PRICE_BOOST 0.25    // ±25% variation around base price (ensures signals even on flat days)

// ========== SOLAR SELLING THRESHOLDS ==========
// Production-ready solar export logic:
// 1. Minimum 5.0 kWh excess = truly meaningful surplus worth selling
// 2. Only sell during EXPENSIVE periods (maximize revenue)
// 3. BUY signals always take priority over SELL (customer savings first)
#define MIN_SURPLUS_TO_SELL_KWH 5.0
#define MIN_PRICE_TO_SELL_SEK 0.01

// ========== HELPER FUNCTIONS ==========

// Swedish grid fees vary by time of day and weekday (time-of-use pricing).
// Most Swedish grid operators (elbolag) use this structure:
// - Night (lågtid): 00:00-06:00 every day
// - Peak (högtid):  17:00-21:00 Monday-Friday only
// - Day (normaltid): All other hours
static double get_grid_fee_for_hour(int hour, int weekday, double low, double normal, double high)
{
    // Night rate: 00:00-06:00 (all days)
    if (hour >= 0 && hour < 6)
        return low;

    // Peak rate: 17:00-21:00 (Monday=1 through Friday=5 only)
    if (hour >= 17 && hour < 21 && weekday >= 1 && weekday <= 5)
        return high;

    // Day rate: everything else (06:00-17:00 all days, 17:00-21:00 weekends, 21:00-24:00 all days)
    return normal;
}

// Calculate actual solar panel temperature from weather conditions.
// Hotter panels = less power output. Wind = cooling = better.
static double calculate_panel_temperature(double air_temp, double sun_intensity, double wind_speed)
{
    // NOCT model (Nominal Operating Cell Temperature) from IEC 61215
    double temp_rise_per_sun = (45.0 - 20.0) / 800.0; // Standard: 45°C at 800 W/m²
    double cooling_effect = 1.0 + WIND_COOLING_FACTOR * wind_speed;
    return air_temp + (temp_rise_per_sun * sun_intensity) / cooling_effect;
}

// Typical Swedish household electricity consumption pattern with 15-minute granularity.
// Returns factor to multiply user's average consumption by.
// Example: user says "I use 1 kWh/h average" → at 18:15 they actually use 1.92 kWh/h (1.6 × 1.2)
static double get_consumption_pattern_quarter(int hour, int minute)
{
    // Base hourly pattern
    double hourly_base = 1.00;  // Default day rate

    if (hour < 7)
        hourly_base = 0.40;     // Night (00-06): Sleeping
    else if (hour < 17)
        hourly_base = 1.00;     // Day (07-16): At work
    else if (hour < 23)
        hourly_base = 1.60;     // Evening (17-22): Peak
    else
        hourly_base = 0.70;     // Late night (23)

    // Add realistic within-hour variation based on typical Swedish household behavior
    double minute_factor = 1.0;

    // Morning rush: 06:30-07:15 (breakfast, shower, coffee maker)
    if (hour == 6 && minute >= 30)
        minute_factor = 1.4;
    if (hour == 7 && minute < 15)
        minute_factor = 1.5;  // Peak morning

    // Lunch: 12:00-12:30 (microwave, stove)
    if (hour == 12 && minute < 30)
        minute_factor = 1.3;

    // Evening cooking: 17:00-19:00 (stove, oven, dishwasher)
    if (hour == 17)
    {
        if (minute < 15)      minute_factor = 1.3;  // Starting prep
        else if (minute < 30) minute_factor = 1.5;  // Cooking starts
        else if (minute < 45) minute_factor = 1.4;
        else                  minute_factor = 1.2;
    }
    if (hour == 18)
    {
        if (minute < 15)      minute_factor = 1.4;  // Peak cooking
        else if (minute < 30) minute_factor = 1.6;  // Absolute peak (stove + oven + microwave)
        else if (minute < 45) minute_factor = 1.2;  // Winding down
        else                  minute_factor = 0.9;  // Cleanup
    }
    if (hour == 19 && minute < 30)
        minute_factor = 1.1;  // Dishwasher running

    // Evening TV/entertainment: 20:00-22:00 (more uniform)
    if (hour >= 20 && hour < 22)
        minute_factor = 1.0;  // Stable TV watching

    // Late evening: 22:00-23:00 (preparing for bed)
    if (hour == 22)
    {
        if (minute < 30)      minute_factor = 0.9;
        else                  minute_factor = 0.7;  // Winding down
    }

    return hourly_base * minute_factor;
}

// Standard comparison function for qsort()
static int compare_doubles(const void *a, const void *b)
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

int Compute_GenerateEnergyPlan(Compute *compute, const ForecastData *forecast, double solarAreaM2, double solarEfficiency, double consumptionKwh,
                               double gridFee_low, double gridFee_normal, double gridFee_high, EnergyData *plan)
{
    if (!compute || !forecast || !plan)
        return -1;

    // Quick check that Compute module is initialized
    pthread_mutex_lock(&compute->mutex);
    bool initialized = compute->isInitialized;
    pthread_mutex_unlock(&compute->mutex);

    if (!initialized)
        return -1;

    int num_quarters = forecast->count;  // Should be 192 (15-min intervals, 48h)
    if (num_quarters <= 0)
    {
        LOG_ERROR("Compute: No forecast data to work with (count=%d). Check if Fetcher and Parser are working.", num_quarters);
        return -1;
    }

    // Calculate hourly costs for threshold determination
    // Spot prices are per hour, so sample one quarter per hour (every 4th entry)
    // ═══════════════════════════════════════════════════════════════════════
    // PRODUCTION ALGORITHM: Quarter-based threshold calculation
    // ═══════════════════════════════════════════════════════════════════════
    // Build cost array for ALL quarters (not just hourly samples)
    // This ensures thresholds match our 15-minute decision granularity

    double actual_costs[MAX_QUARTERS];    // Cost per quarter (for decision logic)
    double sorted_costs[MAX_QUARTERS];    // Sorted costs (for percentile calculation)
    int valid_quarters = 0;

    struct tm tm_buf;
    for (int q = 0; q < num_quarters; q++)
    {
        const ForecastEntry *quarter = &forecast->entries[q];

        // Skip invalid or incomplete data
        if (!quarter->valid || !quarter->hasPriceData)
        {
            actual_costs[q] = 0.0;  // Mark as invalid
            continue;
        }

        // Get time information for grid fee calculation
        if (localtime_r(&quarter->timestamp, &tm_buf) == NULL)
        {
            actual_costs[q] = 0.0;
            continue;
        }

        int hour_of_day = tm_buf.tm_hour;
        int weekday = tm_buf.tm_wday;

        // Calculate ACTUAL cost customer pays for this specific quarter
        double grid_fee = get_grid_fee_for_hour(hour_of_day, weekday, gridFee_low, gridFee_normal, gridFee_high);
        double quarter_cost = (quarter->spotPriceSek + grid_fee + SWEDISH_ENERGY_TAX_SEK_PER_KWH) * (1.0 + SWEDISH_VAT);

        actual_costs[q] = quarter_cost;
        sorted_costs[valid_quarters++] = quarter_cost;
    }

    if (valid_quarters < 4)
    {
        LOG_ERROR("Compute: Insufficient valid quarters (%d < 4). Cannot generate forecast.", valid_quarters);
        return -1;
    }

    // Sort ALL quarter costs to find accurate percentiles
    qsort(sorted_costs, valid_quarters, sizeof(double), compare_doubles);

    int cheap_index = (int)(valid_quarters * CHEAP_QUARTERS_PERCENTILE);
    int median_index = valid_quarters / 2;
    int expensive_index = (int)(valid_quarters * EXPENSIVE_QUARTERS_PERCENTILE);

    // Clamp to array bounds
    if (cheap_index >= valid_quarters) cheap_index = valid_quarters - 1;
    if (median_index >= valid_quarters) median_index = valid_quarters - 1;
    if (expensive_index >= valid_quarters) expensive_index = valid_quarters - 1;

    double cheap_threshold = sorted_costs[cheap_index];
    double median_price = sorted_costs[median_index];
    double expensive_threshold = sorted_costs[expensive_index];

    // Quality check: Ensure meaningful savings (8% below median minimum)
    double min_cheap_price = median_price * (1.0 - MINIMUM_SAVINGS_THRESHOLD);
    if (cheap_threshold > min_cheap_price)
    {
        cheap_threshold = min_cheap_price;
        LOG_INFO("Compute: ⚠ Quality check → cheap threshold adjusted to %.3f kr/kWh (8%% below median)", cheap_threshold);
    }

    // Quality check: Only flag expensive if meaningfully higher
    double min_expensive_price = median_price * (1.0 + MINIMUM_SAVINGS_THRESHOLD);
    if (expensive_threshold < min_expensive_price)
    {
        expensive_threshold = min_expensive_price;
        LOG_INFO("Compute: ⚠ Quality check → expensive threshold adjusted to %.3f kr/kWh (8%% above median)", expensive_threshold);
    }

    // Production logging: Show exactly what customers will see
    int num_buy = 0, num_avoid = 0;
    for (int q = 0; q < valid_quarters; q++)
    {
        if (sorted_costs[q] <= cheap_threshold) num_buy++;
        if (sorted_costs[q] >= expensive_threshold) num_avoid++;
    }

    LOG_INFO("Compute: ═══════════════════════════════════════════════════════");
    LOG_INFO("Compute: 48-HOUR FORECAST ANALYSIS");
    LOG_INFO("Compute: ═══════════════════════════════════════════════════════");
    LOG_INFO("Compute: Quarters analyzed: %d (%.1f hours)", valid_quarters, valid_quarters / 4.0);
    LOG_INFO("Compute: Price range: %.3f - %.3f kr/kWh (spread: %.0f%%)",
             sorted_costs[0], sorted_costs[valid_quarters-1],
             (sorted_costs[valid_quarters-1] - sorted_costs[0]) / sorted_costs[0] * 100.0);
    LOG_INFO("Compute: Decision thresholds:");
    LOG_INFO("Compute:   → BUY window:   ≤ %.3f kr/kWh (cheapest %d quarters ≈ %.1fh)",
             cheap_threshold, num_buy, num_buy / 4.0);
    LOG_INFO("Compute:   → MEDIAN price:   %.3f kr/kWh", median_price);
    LOG_INFO("Compute:   → AVOID window: ≥ %.3f kr/kWh (most expensive %d quarters ≈ %.1fh)",
             expensive_threshold, num_avoid, num_avoid / 4.0);
    LOG_INFO("Compute: ═══════════════════════════════════════════════════════");

    // Generate 15-minute quarter-hour recommendations (48h = 192 quarters)
    // Open-Meteo minutely_15 and Elprisetjustnu provide native 15-min data (from Oct 1, 2025)
    memset(plan, 0, sizeof(EnergyData));
    double total_import = 0.0, total_export = 0.0, total_cost = 0.0;

    for (int i = 0; i < num_quarters && i < 192; i++)
    {
        const ForecastEntry *quarter_data = &forecast->entries[i];
        EnergyDataEntry *plan_entry = &plan->entries[i];

        if (!quarter_data->valid)
            continue;

        struct tm *time_info = localtime(&quarter_data->timestamp);
        int hour_of_day = time_info ? time_info->tm_hour : 12;
        int minute_of_hour = time_info ? time_info->tm_min : 0;

        // Get THIS quarter's actual cost (calculated above)
        double quarter_cost = actual_costs[i];

        // --- Use native 15-minute data (no interpolation needed) ---
        double irradiance = quarter_data->solarIrradiance;
        double temperature = quarter_data->temperature;
        double wind_speed = quarter_data->windSpeed;

        // --- Calculate solar production for this 15-minute slot ---
        double panel_temp = calculate_panel_temperature(temperature, irradiance, wind_speed);
        double temp_efficiency = 1.0 + PANEL_TEMP_COEFFICIENT * (panel_temp - PANEL_TEMP_AT_STANDARD_TEST);

        // Clamp to realistic efficiency range
        if (temp_efficiency < 0.70)
            temp_efficiency = 0.70;
        if (temp_efficiency > 1.10)
            temp_efficiency = 1.10;

        // Solar production for 15 minutes (kWh per quarter-hour)
        double quarter_production = (irradiance / 1000.0) * solarAreaM2 * solarEfficiency *
                                   SOLAR_REAL_WORLD_EFFICIENCY * temp_efficiency * 0.25;

        // --- Calculate consumption for this 15-minute slot with minute-level variation ---
        // Now includes realistic variation within each hour (cooking peaks, morning rush, etc.)
        double quarter_consumption = (consumptionKwh * get_consumption_pattern_quarter(hour_of_day, minute_of_hour)) * 0.25;

        // --- Net energy: negative = need to buy, positive = can sell ---
        double net_energy = quarter_production - quarter_consumption;

        // ═══════════════════════════════════════════════════════════════
        // PRODUCTION DECISION LOGIC: Priority-based signal generation
        // ═══════════════════════════════════════════════════════════════
        EnergyAction recommendation;

        // PRIORITY 0: No price data → IDLE (wait for data)
        if (!quarter_data->hasPriceData || quarter_cost == 0.0)
        {
            recommendation = ACTION_IDLE;
        }
        // PRIORITY 1: Negative prices → ALWAYS BUY (paid to consume!)
        else if (quarter_data->spotPriceSek < 0.0)
        {
            recommendation = ACTION_BUY_FROM_GRID;
            static int negative_logged = 0;
            if (negative_logged < 2)
            {
                LOG_INFO("Compute: [%02d:%02d] ⚡ BUY (negative price!) → %.3f kr/kWh", hour_of_day, minute_of_hour, quarter_data->spotPriceSek);
                negative_logged++;
            }
        }
        // PRIORITY 2: Cheap periods → BUY (optimal time for loads)
        // Customer value: Schedule dishwashers, EVs, heat pumps during these windows
        else if (quarter_cost <= cheap_threshold)
        {
            recommendation = ACTION_BUY_FROM_GRID;
            static int buy_logged = 0;
            if (buy_logged < 5)
            {
                LOG_INFO("Compute: [%02d:%02d] ✓ BUY → %.3f kr/kWh ≤ %.3f threshold (%.0f%% of median, net: %.1f kWh)",
                         hour_of_day, minute_of_hour, quarter_cost, cheap_threshold,
                         (quarter_cost / median_price) * 100.0, net_energy);
                buy_logged++;
            }
        }
        // PRIORITY 3: Large solar surplus during EXPENSIVE periods → SELL
        // Only sell when: (a) meaningful surplus AND (b) price is high enough
        else if (net_energy > MIN_SURPLUS_TO_SELL_KWH && quarter_cost >= expensive_threshold)
        {
            recommendation = ACTION_SELL_TO_GRID;
            total_export += net_energy;
            static int sell_logged = 0;
            if (sell_logged < 3)
            {
                LOG_INFO("Compute: [%02d:%02d] ⚡ SELL → %.1f kWh surplus @ %.3f kr/kWh (expensive period)",
                         hour_of_day, minute_of_hour, net_energy, quarter_cost);
                sell_logged++;
            }
        }
        // PRIORITY 4: Expensive periods → AVOID (defer loads if possible)
        else if (quarter_cost >= expensive_threshold)
        {
            recommendation = ACTION_AVOID_HIGH_PRICE;
            static int avoid_logged = 0;
            if (avoid_logged < 3)
            {
                LOG_INFO("Compute: [%02d:%02d] ⚠ AVOID → %.3f kr/kWh ≥ %.3f threshold (%.0f%% of median)",
                         hour_of_day, minute_of_hour, quarter_cost, expensive_threshold,
                         (quarter_cost / median_price) * 100.0);
                avoid_logged++;
            }
        }
        // PRIORITY 5: Normal periods → IDLE (neither cheap nor expensive)
        else
        {
            recommendation = ACTION_IDLE;
        }

        // --- Track grid imports and costs ---
        if (net_energy < 0.0)
        {
            total_import += -net_energy;
            total_cost += -net_energy * quarter_cost;
        }

        // --- Store results for this quarter ---
        plan_entry->timestamp = quarter_data->timestamp;
        plan_entry->action = recommendation;
        plan_entry->productionKwh = quarter_production;
        plan_entry->consumptionKwh = quarter_consumption;
        plan_entry->spotPrice = quarter_data->spotPriceSek;
        plan_entry->totalCostSek = quarter_cost;
        plan_entry->priceVsAvgPct = median_price > 0.0 ? (quarter_cost - median_price) / median_price * 100.0 : 0.0;
        plan_entry->temperature = temperature;
        plan_entry->windSpeed = wind_speed;
        plan_entry->valid = true;
    }

    plan->count = num_quarters;
    plan->generatedAt = time(NULL);
    plan->totalCostSek = total_cost;
    plan->totalGridImportKwh = total_import;
    plan->totalGridExportKwh = total_export;

    // Find best window for flexible loads
    // Find the longest block of BUY signals with maximum practical value.
    // Balances cost savings against time-of-day convenience for real user adoption.
    // Operates on 15-minute quarters (native resolution from APIs)
    {
        int window_start = -1, window_end = -1;
        double window_cost = 0.0, window_savings = 0.0;
        int window_quarters = 0;
        double best_score = -1.0;  // Now use practicality score instead of just savings

        for (int i = 0; i <= num_quarters; i++)
        {
            // Only consider quarters with actual price data for BUY window
            bool is_cheap_quarter = (i < num_quarters &&
                                    plan->entries[i].valid &&
                                    forecast->entries[i].hasPriceData &&
                                    plan->entries[i].action == ACTION_BUY_FROM_GRID);

            if (is_cheap_quarter)
            {
                if (window_start < 0)
                    window_start = i;
                window_end = i;
                window_cost += plan->entries[i].totalCostSek;
                // Savings = what you WOULD pay at median price vs what you actually pay
                window_savings += (median_price - plan->entries[i].totalCostSek) * plan->entries[i].consumptionKwh;
                window_quarters++;
            }
            else if (window_start >= 0) // End of window
            {
                // Calculate practicality score: balance savings vs convenience
                time_t window_timestamp = plan->entries[window_start].timestamp;
                struct tm window_tm;
                localtime_r(&window_timestamp, &window_tm);
                int start_hour = window_tm.tm_hour;

                // Practicality multiplier based on time of day:
                // 22:00-06:59 → 1.0x (night, acceptable for dishwasher/laundry)
                // 07:00-16:59 → 0.5x (work hours, less likely to start loads)
                // 17:00-21:59 → 1.5x (evening, most convenient for users)
                double practicality_factor = 1.0;
                if (start_hour >= 17 && start_hour < 22)
                    practicality_factor = 1.5;  // Best: evening hours
                else if (start_hour >= 7 && start_hour < 17)
                    practicality_factor = 0.5;  // Worst: day hours when away
                // else: night hours (22-06) = 1.0x baseline

                // Practical value = savings × convenience
                // Example: 8 kr @ 02:00 (1.0x) = 8 points
                //          6 kr @ 23:00 (1.0x) = 6 points → but 6 kr @ 20:00 (1.5x) = 9 points!
                double practical_score = window_savings * practicality_factor;

                if (practical_score > best_score)
                {
                    best_score = practical_score;
                    plan->bestBuyWindow.start = plan->entries[window_start].timestamp;
                    plan->bestBuyWindow.end = plan->entries[window_end].timestamp;
                    plan->bestBuyWindow.hours = window_quarters / 4; // Convert quarters to hours for display
                    plan->bestBuyWindow.avgCostSek = window_cost / window_quarters;
                    plan->bestBuyWindow.savingsSek = window_savings;
                    plan->hasBuyWindow = 1;
                }
                // Reset for next window
                window_start = -1;
                window_end = -1;
                window_cost = 0.0;
                window_savings = 0.0;
                window_quarters = 0;
            }
        }
    }

    if (plan->hasBuyWindow)
    {
        LOG_INFO("Compute: Best time to run flexible loads → %d hour window, saves %.2f SEK", plan->bestBuyWindow.hours, plan->bestBuyWindow.savingsSek);
    }
    else
    {
        LOG_INFO("Compute: No clear cheap window (flat prices or solar covers everything)");
    }

    LOG_INFO("Compute: Forecast complete → %d quarters (%.1f hours), import %.2f kWh, export %.2f kWh, cost %.2f SEK",
             num_quarters, num_quarters / 4.0, total_import, total_export, total_cost);

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
