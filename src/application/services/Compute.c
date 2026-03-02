#include "Compute.h"
#include "Logger.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ---------------------------------------------------------------------------
// Solar model constants
// ---------------------------------------------------------------------------

// Performance ratio: accounts for wiring losses and inverter efficiency.
// 0.75 is the IEC 61724 industry default; calibrate per installation when
// real production data is available.
#define PERFORMANCE_RATIO      0.75

// Minimum net solar surplus (kWh) required to justify grid export.
// Below this the inverter may clip and roundtrip losses exceed the gain.
#define SOLAR_SURPLUS_MIN_KWH  0.05

// ---------------------------------------------------------------------------
// Solar cell temperature model (NOCT — IEC 61215)
// ---------------------------------------------------------------------------

// Nominal Operating Cell Temperature for crystalline silicon panels.
// This is the cell temperature at 800 W/m², 20°C ambient, 1 m/s wind.
// Typical values: 43–47°C. 45°C is the IEC 61215 reference value.
#define NOCT                   45.0

// Standard Test Condition temperature (25°C).
#define TEMP_STC               25.0

// Power temperature coefficient for crystalline silicon (Pmax).
// Typical range: −0.40 % to −0.50 %/°C. IEC 60904-7 default: −0.45 %/°C.
// Positive result means cold bonus (T < 25°C), negative means heat loss.
#define TEMP_COEFF             (-0.0045)

// Wind cooling factor: each additional m/s reduces cell temperature rise.
// Derived from forced-convection heat-transfer models (Koehl et al. 2011).
#define WIND_COOLING_COEFF     0.04

// ---------------------------------------------------------------------------
// Cost model constants (Swedish electricity market)
// ---------------------------------------------------------------------------

// Swedish energy tax (energiskatt), fixed government levy.
#define ENERGY_TAX_SEK_PER_KWH 0.40

// Swedish VAT (moms) on electricity.
#define VAT_RATE               0.25

// ---------------------------------------------------------------------------
// Decision strategy constants
// ---------------------------------------------------------------------------

// BUY is signalled for the cheapest fraction of hours in the forecast window.
// 0.30 → bottom 30% by total consumer cost get a BUY signal.
// Rationale: a typical household has ~6–8 shiftable hours per day; 30% of
// 24 h ≈ 7 h which matches that window without over-signalling.
#define BUY_PERCENTILE         0.30

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static double GetGridFeeForHour(int hour,
                                double gridFee_low,
                                double gridFee_normal,
                                double gridFee_high)
{
    if (hour >= 0 && hour < 7)
        return gridFee_low;     // 00:00–06:59  night rate
    else if (hour >= 7 && hour < 17)
        return gridFee_normal;  // 07:00–16:59  day rate
    else
        return gridFee_high;    // 17:00–23:59  peak rate
}

// Total consumer cost per kWh: spot + grid fee + energy tax + VAT.
static double CalculateTotalCost(double spotPriceSek,
                                 double gridFee,
                                 double energyTax,
                                 double vatRate)
{
    double beforeVat = spotPriceSek + gridFee + energyTax;
    return beforeVat * (1.0 + vatRate);
}

// Cell temperature from ambient conditions using the NOCT model.
//
//   T_cell = T_ambient + (NOCT - 20) / (800 × (1 + k_wind × windSpeed)) × G
//
// The wind term increases the heat-transfer coefficient: stronger wind cools
// the panel surface and reduces the temperature rise above ambient.
//
// At zero irradiance (night) the cell temperature equals ambient — the
// temperature coefficient still applies but production is zero anyway.
static double CalculateCellTemp(double ambientCelsius,
                                double irradianceWm2,
                                double windSpeedMs)
{
    double windDenom = 1.0 + WIND_COOLING_COEFF * windSpeedMs;
    return ambientCelsius + (NOCT - 20.0) / (800.0 * windDenom) * irradianceWm2;
}

// qsort comparator for double arrays.
static int compare_double(const void *a, const void *b)
{
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

int Compute_Initiate(Compute *compute)
{
    if (!compute)
        return -1;

    memset(compute, 0, sizeof(Compute));
    pthread_mutex_init(&compute->mutex, NULL);
    compute->isInitialized = true;

    LOG_INFO("Compute: Initiated");
    return 0;
}

// ---------------------------------------------------------------------------
// Core algorithm
// ---------------------------------------------------------------------------

int Compute_GenerateEnergyPlan(Compute *compute,
                                const ForecastData *forecastData,
                                double solarAreaM2,
                                double solarEfficiency,
                                double consumptionKwh,
                                double gridFee_low,
                                double gridFee_normal,
                                double gridFee_high,
                                EnergyData *plan)
{
    if (!compute || !compute->isInitialized || !forecastData || !plan)
        return -1;

    pthread_mutex_lock(&compute->mutex);

    if (forecastData->count <= 0)
    {
        LOG_ERROR("Compute: No forecast data");
        pthread_mutex_unlock(&compute->mutex);
        return -1;
    }

    // ------------------------------------------------------------------
    // Pass 1 — Pre-compute total consumer cost for every valid hour and
    //           collect the values into a copy array for sorting.
    //
    //  entryCosts[i]  holds the cost for forecastData->entries[i] (0 for
    //                 invalid entries — they are never read in Pass 3).
    //  sortedCosts[]  is a compact array containing only valid entries,
    //                 used to derive percentile thresholds.
    // ------------------------------------------------------------------
    double entryCosts[96]  = {0.0};
    double sortedCosts[96];
    int    sortCount = 0;

    for (int i = 0; i < forecastData->count; i++)
    {
        const ForecastEntry *fc = &forecastData->entries[i];
        if (!fc->valid)
            continue;

        struct tm *tm_info = localtime(&fc->timestamp);
        int hour = tm_info ? tm_info->tm_hour : 12;

        double gridFee = GetGridFeeForHour(hour, gridFee_low, gridFee_normal, gridFee_high);
        double cost    = CalculateTotalCost(fc->spotPriceSek, gridFee,
                                            ENERGY_TAX_SEK_PER_KWH, VAT_RATE);

        entryCosts[i]            = cost;
        sortedCosts[sortCount++] = cost;
    }

    if (sortCount == 0)
    {
        LOG_ERROR("Compute: No valid forecast entries");
        pthread_mutex_unlock(&compute->mutex);
        return -1;
    }

    qsort(sortedCosts, sortCount, sizeof(double), compare_double);

    // ------------------------------------------------------------------
    // Pass 2 — Derive decision thresholds from the cost distribution.
    //
    //  buyThreshold : 30th-percentile total cost.
    //                 Hours at or below this value receive a BUY signal,
    //                 meaning the customer should run flexible loads now.
    //
    //  medianCost   : 50th-percentile total cost.
    //                 Used as a neutral reference to compute per-hour
    //                 savings, letting the customer quantify the benefit
    //                 of acting on a BUY signal.
    // ------------------------------------------------------------------
    int p30idx = (int)(sortCount * BUY_PERCENTILE);
    if (p30idx >= sortCount) p30idx = sortCount - 1;
    int p50idx = sortCount / 2;

    double buyThreshold = sortedCosts[p30idx];
    double medianCost   = sortedCosts[p50idx];

    LOG_INFO("Compute: cost distribution — p30=%.4f  median=%.4f  max=%.4f SEK/kWh",
             buyThreshold, medianCost, sortedCosts[sortCount - 1]);

    // ------------------------------------------------------------------
    // Pass 3 — Per-hour BUY / SELL / IDLE decision.
    //
    //  SELL : Solar surplus AND positive spot price.
    //         When the spot price is negative the grid operator effectively
    //         charges producers; it is better to self-consume than export.
    //
    //  BUY  : Total consumer cost is in the cheapest 30% of the forecast
    //         window.  This is the optimal time to run flexible loads
    //         (EV charging, dishwasher, heat pump, water heater).
    //
    //  IDLE : All remaining hours — consume baseline only, avoid
    //         discretionary load.
    // ------------------------------------------------------------------
    memset(plan, 0, sizeof(EnergyData));

    double totalImport = 0.0;
    double totalExport = 0.0;
    double totalCost   = 0.0;

    int count = forecastData->count;
    for (int i = 0; i < count; i++)
    {
        const ForecastEntry *fc = &forecastData->entries[i];
        EnergyDataEntry     *e  = &plan->entries[i];

        if (!fc->valid)
            continue;

        // --- Solar production with temperature and wind correction ---
        //
        // 1. Cell temperature: NOCT model accounts for irradiance heating
        //    and wind cooling (IEC 61215).
        // 2. Temperature derating: silicon panels lose ~0.45 %/°C above
        //    25°C STC. Cold days give a small bonus (tempFactor > 1.0).
        //    Clamped to [0.70, 1.10] to guard against bad sensor readings.
        // 3. Production: irradiance × area × efficiency × PR × tempFactor.
        //
        // TODO: remaining uncorrected factors — panel tilt/azimuth,
        //       seasonal albedo, shading, and inverter clipping at low load.
        double cellTemp   = CalculateCellTemp(fc->temperature,
                                              fc->solarIrradiance,
                                              fc->windSpeed);
        double tempFactor = 1.0 + TEMP_COEFF * (cellTemp - TEMP_STC);
        if (tempFactor < 0.70) tempFactor = 0.70;
        if (tempFactor > 1.10) tempFactor = 1.10;

        double irradiance = fc->solarIrradiance / 1000.0; // W/m² → kW/m²
        double production = irradiance * solarAreaM2 * solarEfficiency
                            * PERFORMANCE_RATIO * tempFactor;
        double netKwh     = production - consumptionKwh;
        double cost       = entryCosts[i];

        EnergyAction action;

        if (netKwh > SOLAR_SURPLUS_MIN_KWH)
        {
            // Solar surplus available.
            if (fc->spotPriceSek >= 0.0)
            {
                // Positive spot price → exporting is profitable.
                action = ACTION_SELL_TO_GRID;
                totalExport += netKwh;
            }
            else
            {
                // Negative spot price → self-consume; do not export.
                // TODO: if a battery is present, this is the ideal time to
                // charge it rather than export at a loss.
                action = ACTION_IDLE;
            }
        }
        else if (cost <= buyThreshold)
        {
            // No surplus, but cost is in the cheapest 30% of the window.
            // TODO: BUY should be weighted by available flexible load
            // capacity (battery SOC, shiftable load queue) so the signal
            // reflects actionable demand, not just price alone.
            action = ACTION_BUY_FROM_GRID;
        }
        else
        {
            action = ACTION_IDLE;
        }

        // Track grid import for all hours where consumption exceeds production.
        // Both BUY and IDLE hours may import; BUY hours carry extra shiftable load.
        if (netKwh < 0.0)
        {
            totalImport += (-netKwh);
            totalCost   += (-netKwh) * cost;
        }

        e->timestamp          = fc->timestamp;
        e->action             = action;
        e->productionKwh      = production;
        e->consumptionKwh     = consumptionKwh;
        e->spotPrice          = fc->spotPriceSek;
        e->totalCostSek       = cost;
        e->savingsVsMedianSek = medianCost - cost;  // positive → cheaper than median
        e->valid              = true;
    }

    plan->count              = count;
    plan->generatedAt        = time(NULL);
    plan->totalCostSek       = totalCost;
    plan->totalGridImportKwh = totalImport;
    plan->totalGridExportKwh = totalExport;

    LOG_INFO("Compute: Plan ready — %d entries, import=%.2f kWh, export=%.2f kWh, cost=%.2f SEK  "
             "(cell temp model active: NOCT=%.0f°C, Pmax coeff=%.2f%%/°C)",
             count, totalImport, totalExport, totalCost,
             NOCT, TEMP_COEFF * 100.0);

    pthread_mutex_unlock(&compute->mutex);
    return 0;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void Compute_Shutdown(Compute *compute)
{
    if (!compute || !compute->isInitialized)
        return;

    pthread_mutex_destroy(&compute->mutex);
    compute->isInitialized = false;
    LOG_INFO("Compute: Shutdown");
}
