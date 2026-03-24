#include "compute/Compute.h"
#include "sys/Logger.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

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

// Solar export policy: balance between maximizing revenue and grid stability
#define MIN_SURPLUS_TO_SELL_KWH 0.5  // Minimum net surplus per 15-min quarter to trigger export (~2 kWh/h)
#define MIN_PRICE_TO_SELL_SEK 0.01   // Avoid exporting at negative prices

// Forecast uncertainty: high cloud cover makes solar production unreliable.
// When cloud cover exceeds this threshold, raise the SELL price requirement
// so we only export during clearly profitable periods despite uncertain production.
#define CLOUD_COVER_SELL_THRESHOLD 50.0  // % cloud cover above which SELL signal is dampened
#define CLOUD_SELL_THRESHOLD_FACTOR 1.15 // Raise SELL threshold by 15% under high cloud cover (p70 → ~p85)

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

// ========== SOLAR POSITION & POA IRRADIANCE ==========
//
// Calculates Plane-of-Array (POA) irradiance for a tilted solar panel using
// the Hay & Davies transposition model (IEC 61853-1 recommended).
//
// Inputs:
//   ghi       — Global Horizontal Irradiance (W/m²) from Open-Meteo shortwave_radiation
//   dni       — Direct Normal Irradiance (W/m²) from Open-Meteo direct_normal_irradiance
//   dhi       — Diffuse Horizontal Irradiance (W/m²) from Open-Meteo diffuse_radiation
//   latDeg    — Site latitude in decimal degrees (negative = southern hemisphere)
//   lonDeg    — Site longitude in decimal degrees (negative = west)
//   tiltDeg   — Panel tilt: 0=horizontal, 90=vertical
//   azimuthDeg— Panel compass bearing: 0=north, 90=east, 180=south, 270=west
//   t         — UTC Unix timestamp for this 15-minute quarter
//
// Returns POA irradiance in W/m² (≥ 0).
//
// Algorithm:
//   1. Solar declination + equation of time (Spencer 1971)
//   2. Solar hour angle from UTC longitude
//   3. Solar zenith and azimuth angles
//   4. Angle of incidence on tilted panel
//   5. Hay & Davies: POA = DNI×cos(AOI) + DHI×(f×Rb + (1-f)×sky) + GHI×ground

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double CalculatePOA(double ghi, double dni, double dhi,
                            double latDeg, double lonDeg,
                            double tiltDeg, double azimuthDeg,
                            time_t t)
{
    // Nothing to compute at night
    if (ghi <= 0.0 && dni <= 0.0 && dhi <= 0.0)
        return 0.0;

    // Convert angles to radians
    double phi  = latDeg  * M_PI / 180.0;  // latitude
    double beta = tiltDeg * M_PI / 180.0;  // panel tilt

    // Panel azimuth: user gives compass bearing (180=south).
    // Solar engineering convention: 0=south, +90=west, -90=east.
    double gamma_panel = (azimuthDeg - 180.0) * M_PI / 180.0;

    // --- Solar position ---
    struct tm utc;
    gmtime_r(&t, &utc);

    // Day of year (1-based)
    int doy = utc.tm_yday + 1;

    // Day angle (radians)
    double B = 2.0 * M_PI * (doy - 1) / 365.0;

    // Solar declination (Spencer 1971)
    double delta = 0.006918
                 - 0.399912 * cos(B)    + 0.070257 * sin(B)
                 - 0.006758 * cos(2*B)  + 0.000907 * sin(2*B)
                 - 0.002697 * cos(3*B)  + 0.00148  * sin(3*B);

    // Equation of time (minutes, Spencer 1971)
    double EoT = 229.18 * (0.000075
               + 0.001868 * cos(B)   - 0.032077 * sin(B)
               - 0.014615 * cos(2*B) - 0.04089  * sin(2*B));

    // Solar time (hours) from UTC: add longitude correction and EoT
    double utcHours = utc.tm_hour + utc.tm_min / 60.0 + utc.tm_sec / 3600.0;
    double solarTime = utcHours + lonDeg / 15.0 + EoT / 60.0;

    // Hour angle (radians): negative in morning, positive in afternoon
    double omega = (solarTime - 12.0) * M_PI / 12.0;

    // Solar zenith angle
    double cos_zenith = sin(phi)*sin(delta) + cos(phi)*cos(delta)*cos(omega);
    cos_zenith = fmax(-1.0, fmin(1.0, cos_zenith));

    if (cos_zenith <= 0.001)
    {
        // Sun below horizon — only diffuse sky contribution remains
        double sky_diffuse = dhi * (1.0 + cos(beta)) / 2.0;
        return fmax(0.0, sky_diffuse);
    }

    double sin_zenith = sqrt(fmax(0.0, 1.0 - cos_zenith * cos_zenith));

    // Solar azimuth from south (positive = west), radians
    double cos_azimuth_sol = (sin(delta)*cos(phi) - cos(delta)*sin(phi)*cos(omega))
                             / fmax(sin_zenith, 0.001);
    cos_azimuth_sol = fmax(-1.0, fmin(1.0, cos_azimuth_sol));
    double gamma_s = acos(cos_azimuth_sol);
    if (omega < 0.0)
        gamma_s = -gamma_s;  // morning: sun is east of south (negative)

    // Angle of incidence on tilted panel surface
    double cos_aoi = cos_zenith * cos(beta)
                   + sin_zenith * sin(beta) * cos(gamma_s - gamma_panel);
    cos_aoi = fmax(0.0, cos_aoi);  // negative AOI means sun behind panel

    // --- Hay & Davies transposition model ---
    // Beam component
    double poa_beam = dni * cos_aoi;

    // Anisotropy index: fraction of diffuse that behaves like direct beam (circumsolar)
    double ghi_safe = fmax(ghi, dni * cos_zenith + dhi);  // GHI >= DNI*cos(θz)+DHI
    double f_hay = (ghi_safe > 0.01) ? fmin(dni / ghi_safe, 1.0) : 0.0;

    // Rb: ratio of beam on tilted plane to beam on horizontal
    double Rb = cos_aoi / fmax(cos_zenith, 0.001);
    Rb = fmin(Rb, 10.0);  // cap to avoid numerical explosion at near-horizon

    // Diffuse component (Hay & Davies: circumsolar + isotropic sky)
    double sky_factor   = (1.0 + cos(beta)) / 2.0;
    double poa_diffuse  = dhi * (f_hay * Rb + (1.0 - f_hay) * sky_factor);

    // Ground-reflected component (albedo = 0.2, typical for grass/gravel)
    double ground_factor = (1.0 - cos(beta)) / 2.0;
    double poa_ground    = ghi * 0.2 * ground_factor;

    return fmax(0.0, poa_beam + poa_diffuse + poa_ground);
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

int Compute_GenerateEnergyPlan(Compute *compute, const ForecastData *forecast, double solarAreaM2, double solarEfficiency, double consumptionKwh, double gridFeeLow, double gridFeeNormal, double gridFeeHigh, double panelTiltDeg, double panelAzimuthDeg, double latitude, double longitude, EnergyData *plan)
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
    if (numQuarters > MAX_QUARTERS)
        numQuarters = MAX_QUARTERS;
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
    LOG_INFO("Compute: Price range: %.3f - %.3f kr/kWh (spread: %.0f%%)", sortedCosts[0], sortedCosts[validQuarters-1], sortedCosts[0] != 0.0 ? (sortedCosts[validQuarters-1] - sortedCosts[0]) / sortedCosts[0] * 100.0 : 0.0);
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
        double ghi         = quarterData->solarIrradiance;
        double dni         = quarterData->directNormalIrradiance;
        double dhi         = quarterData->diffuseRadiation;
        double temperature = quarterData->temperature;
        double windSpeed   = quarterData->windSpeed;

        // Plane-of-Array irradiance via Hay & Davies transposition model.
        // Uses DNI + DHI + GHI, panel tilt/azimuth, and solar position from lat/lon.
        // Falls back gracefully to GHI-based estimate if DNI/DHI are zero (data gap).
        double poa = CalculatePOA(ghi, dni, dhi,
                                  latitude, longitude,
                                  panelTiltDeg, panelAzimuthDeg,
                                  quarterData->timestamp);

        // Solar production model with temperature derating (IEC 61215 NOCT)
        double panelTemp = CalculatePanelTemperature(temperature, poa, windSpeed);
        double tempEfficiency = 1.0 + PANEL_TEMP_COEFFICIENT * (panelTemp - PANEL_TEMP_AT_STANDARD_TEST);
        if (tempEfficiency < 0.70) tempEfficiency = 0.70;
        if (tempEfficiency > 1.10) tempEfficiency = 1.10;

        double quarterProduction = (poa / 1000.0) * solarAreaM2 * solarEfficiency * SOLAR_REAL_WORLD_EFFICIENCY * tempEfficiency * 0.25;

        // Load profile application
        double quarterConsumption = (consumptionKwh * GetConsumptionPatternQuarter(hourOfDay, minuteOfHour)) * 0.25;
        double netEnergy = quarterProduction - quarterConsumption;

        // Forecast uncertainty: raise SELL threshold when cloud cover is high.
        // High cloud cover (>50%) means solar production is unreliable — require a
        // stronger price signal before recommending export to avoid selling during
        // periods where production may unexpectedly drop.
        double effectiveSellThreshold = (quarterData->cloudCover > CLOUD_COVER_SELL_THRESHOLD)
            ? expensiveThreshold * CLOUD_SELL_THRESHOLD_FACTOR
            : expensiveThreshold;

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
                LOG_INFO("Compute: [%02d:%02d] BUY (negative price!) → %.3f kr/kWh", hourOfDay, minuteOfHour, quarterData->spotPriceSek);
                negativeLogged++;
            }
        }
        else if (quarterCost <= cheapThreshold)
        {
            recommendation = ACTION_BUY_FROM_GRID;
            if (buyLogged < 5)
            {
                LOG_INFO("Compute: [%02d:%02d] BUY → %.3f kr/kWh ≤ %.3f threshold (%.0f%% of median, net: %.1f kWh)", hourOfDay, minuteOfHour, quarterCost, cheapThreshold, (quarterCost / medianPrice) * 100.0, netEnergy);
                buyLogged++;
            }
        }
        else if (netEnergy > MIN_SURPLUS_TO_SELL_KWH &&
                 quarterData->spotPriceSek >= MIN_PRICE_TO_SELL_SEK &&
                 quarterCost >= effectiveSellThreshold)
        {
            // Optimal sell: high price AND surplus
            recommendation = ACTION_SELL_TO_GRID;
            totalExport += netEnergy;
            if (sellLogged < 3)
            {
                if (quarterData->cloudCover > CLOUD_COVER_SELL_THRESHOLD)
                    LOG_INFO("Compute: [%02d:%02d] SELL (optimal) → %.1f kWh surplus @ %.3f kr/kWh (cloudy %.0f%% — raised threshold %.3f)", hourOfDay, minuteOfHour, netEnergy, quarterCost, quarterData->cloudCover, effectiveSellThreshold);
                else
                    LOG_INFO("Compute: [%02d:%02d] SELL (optimal) → %.1f kWh surplus @ %.3f kr/kWh (clear sky %.0f%%)", hourOfDay, minuteOfHour, netEnergy, quarterCost, quarterData->cloudCover);
                sellLogged++;
            }
        }
        else if (netEnergy > MIN_SURPLUS_TO_SELL_KWH &&
                 quarterData->spotPriceSek >= MIN_PRICE_TO_SELL_SEK &&
                 quarterCost >= medianPrice)
        {
            // Surplus sell: solar produces more than consumed, price at or above median.
            // Below-median periods stay IDLE — not worth actively recommending export at a discount.
            recommendation = ACTION_SELL_TO_GRID;
            totalExport += netEnergy;
            if (sellLogged < 3)
            {
                LOG_INFO("Compute: [%02d:%02d] SELL (surplus) → %.1f kWh @ %.3f kr/kWh (+%.0f%% vs median)", hourOfDay, minuteOfHour, netEnergy, quarterCost, (quarterCost - medianPrice) / medianPrice * 100.0);
                sellLogged++;
            }
        }
        else if (quarterCost >= expensiveThreshold)
        {
            recommendation = ACTION_AVOID_HIGH_PRICE;
            if (avoidLogged < 3)
            {
                LOG_INFO("Compute: [%02d:%02d] AVOID → %.3f kr/kWh ≥ %.3f threshold (%.0f%% of median)", hourOfDay, minuteOfHour, quarterCost, expensiveThreshold, (quarterCost / medianPrice) * 100.0);
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
                    plan->bestBuyWindow.end = plan->entries[windowEnd].timestamp + 15 * 60;
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
