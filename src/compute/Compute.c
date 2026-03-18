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
// - Lågtid (night):  00:00-06:00 daily
// - Högtid (peak):   17:00-21:00 weekdays only
// - Normaltid (day): all other periods
static double get_grid_fee_for_hour(int hour, int weekday, double low, double normal, double high)
{
    if (hour >= 0 && hour < 6)
        return low;
    if (hour >= 17 && hour < 21 && weekday >= 1 && weekday <= 5)
        return high;
    return normal;
}

// NOCT-based panel temperature model (IEC 61215)
// Higher temperatures reduce photovoltaic efficiency; wind provides convective cooling
static double calculate_panel_temperature(double air_temp, double sun_intensity, double wind_speed)
{
    double temp_rise_per_sun = (45.0 - 20.0) / 800.0;
    double cooling_effect = 1.0 + WIND_COOLING_FACTOR * wind_speed;
    return air_temp + (temp_rise_per_sun * sun_intensity) / cooling_effect;
}

// Consumption profile for Swedish residential users (15-minute resolution)
// Captures daily behavioral patterns: morning rush, daytime baseline, evening peak
static double get_consumption_pattern_quarter(int hour, int minute)
{
    double hourly_base = 1.00;
    if (hour < 7)        hourly_base = 0.40;
    else if (hour < 17)  hourly_base = 1.00;
    else if (hour < 23)  hourly_base = 1.60;
    else                 hourly_base = 0.70;

    double minute_factor = 1.0;

    // Morning: 06:30-07:15
    if (hour == 6 && minute >= 30) minute_factor = 1.4;
    if (hour == 7 && minute < 15)  minute_factor = 1.5;

    // Middag: 12:00-12:30
    if (hour == 12 && minute < 30) minute_factor = 1.3;

    // Kvällsmatlagning: 17:00-19:00
    if (hour == 17)
    {
        if (minute < 15)      minute_factor = 1.3;
        else if (minute < 30) minute_factor = 1.5;
        else if (minute < 45) minute_factor = 1.4;
        else                  minute_factor = 1.2;
    }
    if (hour == 18)
    {
        if (minute < 15)      minute_factor = 1.4;
        else if (minute < 30) minute_factor = 1.6;
        else if (minute < 45) minute_factor = 1.2;
        else                  minute_factor = 0.9;
    }
    if (hour == 19 && minute < 30) minute_factor = 1.1;

    if (hour >= 20 && hour < 22) minute_factor = 1.0;

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
    // Quarter-hour cost calculation: spot price + tariff + tax + VAT
    // Percentile thresholds computed at 15-minute resolution for accurate signal generation
    double actual_costs[MAX_QUARTERS];
    double sorted_costs[MAX_QUARTERS];
    int valid_quarters = 0;

    struct tm tm_buf;
    for (int q = 0; q < num_quarters; q++)
    {
        const ForecastEntry *quarter = &forecast->entries[q];

        if (!quarter->valid || !quarter->hasPriceData)
        {
            actual_costs[q] = 0.0;
            continue;
        }

        if (localtime_r(&quarter->timestamp, &tm_buf) == NULL)
        {
            actual_costs[q] = 0.0;
            continue;
        }

        int hour_of_day = tm_buf.tm_hour;
        int weekday = tm_buf.tm_wday;

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

    qsort(sorted_costs, valid_quarters, sizeof(double), compare_doubles);

    int cheap_index = (int)(valid_quarters * CHEAP_QUARTERS_PERCENTILE);
    int median_index = valid_quarters / 2;
    int expensive_index = (int)(valid_quarters * EXPENSIVE_QUARTERS_PERCENTILE);

    if (cheap_index >= valid_quarters) cheap_index = valid_quarters - 1;
    if (median_index >= valid_quarters) median_index = valid_quarters - 1;
    if (expensive_index >= valid_quarters) expensive_index = valid_quarters - 1;

    double cheap_threshold = sorted_costs[cheap_index];
    double median_price = sorted_costs[median_index];
    double expensive_threshold = sorted_costs[expensive_index];

    // Quality filter: require minimum deviation from median to ensure signal value
    double min_cheap_price = median_price * (1.0 - MINIMUM_SAVINGS_THRESHOLD);
    if (cheap_threshold > min_cheap_price)
    {
        cheap_threshold = min_cheap_price;
        LOG_INFO("Compute: Threshold adjusted → BUY at %.3f kr/kWh (8%% median discount)", cheap_threshold);
    }

    double min_expensive_price = median_price * (1.0 + MINIMUM_SAVINGS_THRESHOLD);
    if (expensive_threshold < min_expensive_price)
    {
        expensive_threshold = min_expensive_price;
        LOG_INFO("Compute: Threshold adjusted → AVOID at %.3f kr/kWh (8%% median premium)", expensive_threshold);
    }

    int num_buy = 0, num_avoid = 0;
    for (int q = 0; q < valid_quarters; q++)
    {
        if (sorted_costs[q] <= cheap_threshold) num_buy++;
        if (sorted_costs[q] >= expensive_threshold) num_avoid++;
    }

    int negative_logged = 0, buy_logged = 0, sell_logged = 0, avoid_logged = 0;

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

    // Signal generation loop: evaluate each 15-minute period independently
    memset(plan, 0, sizeof(EnergyData));
    double total_import = 0.0, total_export = 0.0, total_cost = 0.0;

    for (int i = 0; i < num_quarters && i < 192; i++)
    {
        const ForecastEntry *quarter_data = &forecast->entries[i];
        EnergyDataEntry *plan_entry = &plan->entries[i];

        if (!quarter_data->valid)
            continue;

        struct tm time_info_buf;
        struct tm *time_info = localtime_r(&quarter_data->timestamp, &time_info_buf);
        int hour_of_day = time_info ? time_info->tm_hour : 12;
        int minute_of_hour = time_info ? time_info->tm_min : 0;

        double quarter_cost = actual_costs[i];
        double irradiance = quarter_data->solarIrradiance;
        double temperature = quarter_data->temperature;
        double wind_speed = quarter_data->windSpeed;

        // Solar production model with temperature derating
        double panel_temp = calculate_panel_temperature(temperature, irradiance, wind_speed);
        double temp_efficiency = 1.0 + PANEL_TEMP_COEFFICIENT * (panel_temp - PANEL_TEMP_AT_STANDARD_TEST);
        if (temp_efficiency < 0.70) temp_efficiency = 0.70;
        if (temp_efficiency > 1.10) temp_efficiency = 1.10;

        double quarter_production = (irradiance / 1000.0) * solarAreaM2 * solarEfficiency *
                                   SOLAR_REAL_WORLD_EFFICIENCY * temp_efficiency * 0.25;

        // Load profile application
        double quarter_consumption = (consumptionKwh * get_consumption_pattern_quarter(hour_of_day, minute_of_hour)) * 0.25;
        double net_energy = quarter_production - quarter_consumption;

        // Signal classification with priority ordering
        EnergyAction recommendation;

        if (!quarter_data->hasPriceData || quarter_cost == 0.0)
        {
            recommendation = ACTION_IDLE;
        }
        else if (quarter_data->spotPriceSek < 0.0)
        {
            recommendation = ACTION_BUY_FROM_GRID;
            if (negative_logged < 2)
            {
                LOG_INFO("Compute: [%02d:%02d] ⚡ BUY (negative price!) → %.3f kr/kWh", hour_of_day, minute_of_hour, quarter_data->spotPriceSek);
                negative_logged++;
            }
        }
        else if (quarter_cost <= cheap_threshold)
        {
            recommendation = ACTION_BUY_FROM_GRID;
            if (buy_logged < 5)
            {
                LOG_INFO("Compute: [%02d:%02d] ✓ BUY → %.3f kr/kWh ≤ %.3f threshold (%.0f%% of median, net: %.1f kWh)",
                         hour_of_day, minute_of_hour, quarter_cost, cheap_threshold,
                         (quarter_cost / median_price) * 100.0, net_energy);
                buy_logged++;
            }
        }
        else if (net_energy > MIN_SURPLUS_TO_SELL_KWH && quarter_cost >= expensive_threshold)
        {
            recommendation = ACTION_SELL_TO_GRID;
            total_export += net_energy;
            if (sell_logged < 3)
            {
                LOG_INFO("Compute: [%02d:%02d] ⚡ SELL → %.1f kWh surplus @ %.3f kr/kWh (expensive period)",
                         hour_of_day, minute_of_hour, net_energy, quarter_cost);
                sell_logged++;
            }
        }
        else if (quarter_cost >= expensive_threshold)
        {
            recommendation = ACTION_AVOID_HIGH_PRICE;
            if (avoid_logged < 3)
            {
                LOG_INFO("Compute: [%02d:%02d] ⚠ AVOID → %.3f kr/kWh ≥ %.3f threshold (%.0f%% of median)",
                         hour_of_day, minute_of_hour, quarter_cost, expensive_threshold,
                         (quarter_cost / median_price) * 100.0);
                avoid_logged++;
            }
        }
        else
        {
            recommendation = ACTION_IDLE;
        }

        if (net_energy < 0.0)
        {
            total_import += -net_energy;
            total_cost += -net_energy * quarter_cost;
        }

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
                    plan->bestBuyWindow.durationMinutes = window_quarters * 15;
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
        LOG_INFO("Compute: Best time to run flexible loads → %d min window, saves %.2f SEK", plan->bestBuyWindow.durationMinutes, plan->bestBuyWindow.savingsSek);
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
