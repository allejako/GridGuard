#include "compute/Compute.h"
#include "sys/Logger.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
// We tell customers to BUY (run appliances) during the cheapest 30% of hours.
// This is a balance: too strict (10%) and you rarely get signals,
// too loose (50%) and "cheap" isn't actually cheap.
#define CHEAP_HOURS_PERCENTILE 0.30     // Bottom 30% = "cheap"
#define EXPENSIVE_HOURS_PERCENTILE 0.70 // Top 30% = "expensive"

// Quality check: don't signal "cheap" unless it's below median.
// With demo mode enabled, we use 5% threshold to ensure signals appear.
#define MINIMUM_SAVINGS_THRESHOLD 0.05 // 5% discount required (works with demo boost)

// ========== DEMO MODE ==========
// On flat-price days, add artificial variation to make demo more interesting.
// This multiplier is applied based on time-of-day to simulate typical patterns.
#define DEMO_MODE_ENABLED 0     // Set to 0 to disable demo boost
#define DEMO_PRICE_BOOST 0.25    // ±25% variation around base price (ensures signals even on flat days)

// ========== SOLAR SELLING THRESHOLDS ==========
// Only recommend selling solar surplus if:
// 1. You have at least 50 Wh excess (inverter losses below this)
// 2. Spot price is positive (negative = you PAY to export!)
#define MIN_SURPLUS_TO_SELL_KWH 0.05
#define MIN_PRICE_TO_SELL_SEK 0.05

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
    // For each hour, compute what the customer ACTUALLY pays per kWh:
    // Total = (spot price + grid fee + energy tax) × (1 + VAT)
    int num_hours = (num_quarters + 3) / 4;  // Round up: 192 quarters = 48 hours
    double actual_costs[48];  // Store hourly costs (one per hour, up to 48h)
    double sorted_costs[48];
    int valid_hours = 0;

    // OPTIMIZATION: Pre-compute hour-of-day and weekday arrays for all hours
    int hours_of_day[48];
    int weekdays[48];
    struct tm tm_buf;
    for (int h = 0; h < num_hours; h++)
    {
        int quarter_idx = h * 4;  // First quarter of this hour
        if (quarter_idx < num_quarters && localtime_r(&forecast->entries[quarter_idx].timestamp, &tm_buf) != NULL)
        {
            hours_of_day[h] = tm_buf.tm_hour;
            weekdays[h] = tm_buf.tm_wday;  // 0=Sunday, 1=Monday, ..., 6=Saturday
        }
        else
        {
            hours_of_day[h] = 12;  // Fallback to noon if conversion fails
            weekdays[h] = 3;        // Fallback to Wednesday (mid-week)
        }
    }

    // Build hourly cost array (sample first quarter of each hour for spot price)
    for (int h = 0; h < num_hours; h++)
    {
        int quarter_idx = h * 4;  // Sample first quarter of each hour for price
        if (quarter_idx >= num_quarters)
            break;

        const ForecastEntry *quarter = &forecast->entries[quarter_idx];
        if (!quarter->valid)
            continue;

        int hour_of_day = hours_of_day[h];  // Array access instead of syscall, 100× faster
        int weekday = weekdays[h];

        double grid_fee = get_grid_fee_for_hour(hour_of_day, weekday, gridFee_low, gridFee_normal, gridFee_high);
        double total_cost = (quarter->spotPriceSek + grid_fee + SWEDISH_ENERGY_TAX_SEK_PER_KWH) * (1.0 + SWEDISH_VAT);

#if DEMO_MODE_ENABLED
        // Demo boost: Add time-of-day variation to make flat-price days more interesting
        // IMPORTANT: Applied BEFORE threshold calculation so percentiles reflect the boost!
        // Night (00-06): -25% (cheapest → BUY signals)
        // Morning (07-11): +8%
        // Day (12-16): 0% (baseline)
        // Evening peak (17-20): +25% (most expensive → AVOID signals)
        // Late evening (21-23): +8%
        double demo_multiplier = 1.0;
        if (hour_of_day >= 0 && hour_of_day < 7) {
            demo_multiplier = 1.0 - DEMO_PRICE_BOOST;  // Night: cheap
        } else if (hour_of_day >= 7 && hour_of_day < 12) {
            demo_multiplier = 1.0 + (DEMO_PRICE_BOOST * 0.33);  // Morning: slightly higher
        } else if (hour_of_day >= 17 && hour_of_day < 21) {
            demo_multiplier = 1.0 + DEMO_PRICE_BOOST;  // Evening peak: expensive
        } else if (hour_of_day >= 21 && hour_of_day < 24) {
            demo_multiplier = 1.0 + (DEMO_PRICE_BOOST * 0.33);  // Late: slightly higher
        }
        total_cost *= demo_multiplier;
#endif

        // Store boosted hourly cost
        actual_costs[h] = total_cost;
        sorted_costs[valid_hours++] = total_cost;  // Now includes demo boost!
    }

    if (valid_hours == 0)
    {
        LOG_ERROR("Compute: All forecast hours are invalid (checked %d hours from %d quarters, 0 valid). Parser may have failed to validate data.", num_hours, num_quarters);
        return -1;
    }

    // Find cheap and expensive hours
    // Sort all costs to find percentiles (cheap 30%, expensive 30%)
    qsort(sorted_costs, valid_hours, sizeof(double), compare_doubles);

    int cheap_index = (int)(valid_hours * CHEAP_HOURS_PERCENTILE);
    int median_index = valid_hours / 2;
    int expensive_index = (int)(valid_hours * EXPENSIVE_HOURS_PERCENTILE);

    // Clamp to array bounds
    if (cheap_index >= valid_hours)
        cheap_index = valid_hours - 1;
    if (median_index >= valid_hours)
        median_index = valid_hours - 1;
    if (expensive_index >= valid_hours)
        expensive_index = valid_hours - 1;

    double cheap_threshold = sorted_costs[cheap_index];
    double median_price = sorted_costs[median_index];
    double expensive_threshold = sorted_costs[expensive_index];

    // Quality check: Only signal "cheap" if it's meaningfully cheaper than median.
    // On days where prices are flat, p30 ≈ median, so "cheap" isn't actually cheap.
    double min_cheap_price = median_price * (1.0 - MINIMUM_SAVINGS_THRESHOLD);
    if (cheap_threshold > min_cheap_price)
        cheap_threshold = min_cheap_price;

    // Same for expensive: only warn if meaningfully more expensive
    double min_expensive_price = median_price * (1.0 + MINIMUM_SAVINGS_THRESHOLD);
    if (expensive_threshold < min_expensive_price)
        expensive_threshold = min_expensive_price;

    LOG_INFO("Compute: Price analysis → cheap: %.2f SEK/kWh, median: %.2f, expensive: %.2f", cheap_threshold, median_price, expensive_threshold);

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

        // Find corresponding hourly cost (prices matched during parsing)
        int hour_idx = i / 4;  // 4 quarters per hour
        if (hour_idx >= num_hours)
            hour_idx = num_hours - 1;
        double hourly_cost = actual_costs[hour_idx];

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

        // --- Decide recommendation ---
        EnergyAction recommendation;

        // Negative prices = essentially free electricity (or paid to consume)
        // Always recommend BUY regardless of consumption patterns
        if (quarter_data->spotPriceSek < 0.0)
        {
            recommendation = ACTION_BUY_FROM_GRID;
        }
        else if (net_energy > MIN_SURPLUS_TO_SELL_KWH && quarter_data->spotPriceSek >= MIN_PRICE_TO_SELL_SEK)
        {
            recommendation = ACTION_SELL_TO_GRID;
            total_export += net_energy;
        }
        else if (hourly_cost <= cheap_threshold)
        {
            recommendation = ACTION_BUY_FROM_GRID;
        }
        else if (hourly_cost >= expensive_threshold)
        {
            recommendation = ACTION_AVOID_HIGH_PRICE;
        }
        else
        {
            recommendation = ACTION_IDLE;
        }

        // --- Track grid imports and costs ---
        if (net_energy < 0.0)
        {
            total_import += -net_energy;
            total_cost += -net_energy * hourly_cost;
        }

        // --- Store results for this quarter ---
        plan_entry->timestamp = quarter_data->timestamp;
        plan_entry->action = recommendation;
        plan_entry->productionKwh = quarter_production;
        plan_entry->consumptionKwh = quarter_consumption;
        plan_entry->spotPrice = quarter_data->spotPriceSek;
        plan_entry->totalCostSek = hourly_cost;
        plan_entry->priceVsAvgPct = median_price > 0.0 ? (hourly_cost - median_price) / median_price * 100.0 : 0.0;
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
            bool is_cheap_quarter = (i < num_quarters && plan->entries[i].valid && plan->entries[i].action == ACTION_BUY_FROM_GRID);

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
