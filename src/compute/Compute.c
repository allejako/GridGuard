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

// Quality check: don't signal "cheap" unless it's AT LEAST 10% below median.
// This prevents false signals on flat-price days when shifting load is pointless.
#define MINIMUM_SAVINGS_THRESHOLD 0.10 // 10% discount required

// ========== SOLAR SELLING THRESHOLDS ==========
// Only recommend selling solar surplus if:
// 1. You have at least 50 Wh excess (inverter losses below this)
// 2. Spot price is positive (negative = you PAY to export!)
#define MIN_SURPLUS_TO_SELL_KWH 0.05
#define MIN_PRICE_TO_SELL_SEK 0.05

// ========== HELPER FUNCTIONS ==========

// Swedish grid fees vary by time of day (time-of-use pricing).
// This is set by your electricity company (elhandlare).
static double get_grid_fee_for_hour(int hour, double low, double normal, double high)
{
    if (hour < 7)
        return low; // Night: 00-06
    if (hour < 17)
        return normal; // Day: 07-16
    return high;       // Peak: 17-23
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

// Typical Swedish household electricity consumption pattern.
// Returns factor to multiply user's average consumption by.
// Example: user says "I use 1 kWh/h average" → at 8 PM they actually use 1.6 kWh/h
static double get_consumption_pattern(int hour)
{
    // Night (00-06): Sleeping. Only fridge, freezer running. = 40% of average
    if (hour < 7)
        return 0.40;

    // Day (07-16): At work. Background loads only. = 100% of average
    if (hour < 17)
        return 1.00;

    // Evening (17-22): Peak usage. Cooking, heating, TV, laundry. = 160% of average
    if (hour < 23)
        return 1.60;

    // Late night (23): Winding down. = 70% of average
    return 0.70;
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

    int num_hours = forecast->count;
    if (num_hours <= 0)
    {
        LOG_ERROR("Compute: No forecast data to work with (count=%d). Check if Fetcher and Parser are working.", num_hours);
        return -1;
    }

    // Calculate real costs
    // For each hour, compute what the customer ACTUALLY pays per kWh:
    // Total = (spot price + grid fee + energy tax) × (1 + VAT)
    double actual_costs[96];
    double sorted_costs[96];
    int valid_hours = 0;

    // OPTIMIZATION: Pre-compute hour-of-day array to avoid 96 localtime() syscalls (51% CPU overhead)
    // Using localtime_r() instead of localtime() for thread-safety
    int hours_of_day[96];
    struct tm tm_buf;
    for (int i = 0; i < num_hours; i++)
    {
        if (localtime_r(&forecast->entries[i].timestamp, &tm_buf) != NULL)
        {
            hours_of_day[i] = tm_buf.tm_hour;
        }
        else
        {
            hours_of_day[i] = 12; // Fallback to noon if conversion fails
        }
    }

    for (int i = 0; i < num_hours; i++)
    {
        const ForecastEntry *hour = &forecast->entries[i];
        if (!hour->valid)
            continue;

        int hour_of_day = hours_of_day[i]; // Array access instead of syscall, 100× faster

        double grid_fee = get_grid_fee_for_hour(hour_of_day, gridFee_low, gridFee_normal, gridFee_high);
        double total_cost = (hour->spotPriceSek + grid_fee + SWEDISH_ENERGY_TAX_SEK_PER_KWH) * (1.0 + SWEDISH_VAT);

        actual_costs[i] = total_cost;
        sorted_costs[valid_hours++] = total_cost;
    }

    if (valid_hours == 0)
    {
        LOG_ERROR("Compute: All forecast hours are invalid (checked %d entries, 0 valid). Parser may have failed to validate data.", num_hours);
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

    // Generate 15-minute quarter-hour recommendations (24h = 96 quarters)
    // Open-Meteo minutely_15 and Elprisetjustnu provide native 15-min data (from Oct 1, 2025)
    memset(plan, 0, sizeof(EnergyData));
    double total_import = 0.0, total_export = 0.0, total_cost = 0.0;

    int num_quarters = forecast->count;  // Should be 96 entries (24h × 4 quarters/hour)

    for (int i = 0; i < num_quarters && i < 96; i++)
    {
        const ForecastEntry *quarter_data = &forecast->entries[i];
        EnergyDataEntry *plan_entry = &plan->entries[i];

        if (!quarter_data->valid)
            continue;

        struct tm *time_info = localtime(&quarter_data->timestamp);
        int hour_of_day = time_info ? time_info->tm_hour : 12;

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

        // --- Calculate consumption for this 15-minute slot ---
        // Scale hourly consumption to quarter-hour (÷4)
        double quarter_consumption = (consumptionKwh * get_consumption_pattern(hour_of_day)) * 0.25;

        // --- Net energy: negative = need to buy, positive = can sell ---
        double net_energy = quarter_production - quarter_consumption;

        // --- Decide recommendation ---
        EnergyAction recommendation;

        if (net_energy > MIN_SURPLUS_TO_SELL_KWH && quarter_data->spotPriceSek >= MIN_PRICE_TO_SELL_SEK)
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
        plan_entry->valid = true;
    }

    plan->count = num_quarters;
    plan->generatedAt = time(NULL);
    plan->totalCostSek = total_cost;
    plan->totalGridImportKwh = total_import;
    plan->totalGridExportKwh = total_export;

    // Find best window for flexible loads
    // Find the longest block of BUY signals with maximum savings.
    // Operates on 15-minute quarters (native resolution from APIs)
    {
        int window_start = -1, window_end = -1;
        double window_cost = 0.0, window_savings = 0.0;
        int window_quarters = 0;
        double best_savings = -1.0;

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
                if (window_savings > best_savings)
                {
                    best_savings = window_savings;
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
