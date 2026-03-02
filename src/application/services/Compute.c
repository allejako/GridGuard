#include "Compute.h"
#include "Logger.h"
#include <string.h>
#include <time.h>

// TODO: Replace with calibrated model.
// Performance ratio accounts for wiring losses, temperature drift and inverter
// efficiency. 0.75 is a rough industry default — real value depends on panel
// orientation, shading, cable length and inverter spec.
#define PERFORMANCE_RATIO 0.75

// TODO: Replace with a proper price-signal strategy.
// Buying at 80% of the day's average is a placeholder. A real strategy should
// consider tomorrow's prices (when available), time-of-use tariffs, battery
// state-of-charge and grid peak hours.
#define BUY_PRICE_FRACTION 0.80

// Energy tax (Swedish electricity tax)
#define ENERGY_TAX_SEK_PER_KWH 0.40

// VAT rate (Swedish moms)
#define VAT_RATE 0.25

// Get grid fee based on hour of day (Swedish time-of-use tariff)
static double GetGridFeeForHour(int hour, double gridFee_low, double gridFee_normal, double gridFee_high)
{
    if (hour >= 0 && hour < 7)
        return gridFee_low;     // 00:00-06:59 night rate
    else if (hour >= 7 && hour < 17)
        return gridFee_normal;  // 07:00-16:59 day rate
    else
        return gridFee_high;    // 17:00-23:59 peak rate
}

// Calculate total cost per kWh including spot price, grid fee, energy tax and VAT
static double CalculateTotalCost(double spotPriceSek, double gridFee, double energyTax, double vatRate)
{
    // Total before VAT = spot + grid fee + energy tax
    double beforeVat = spotPriceSek + gridFee + energyTax;

    // Add VAT
    double vat = beforeVat * vatRate;

    // Total cost to consumer
    return beforeVat + vat;
}

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

int Compute_GenerateEnergyPlan(Compute *compute, const ForecastData *forecastData, double solarAreaM2, double solarEfficiency, double consumptionKwh, double gridFee_low, double gridFee_normal, double gridFee_high, EnergyData *plan)
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

    // --- Pass 1: calculate average total cost (used as BUY threshold) ---
    // Total cost includes: spot price + grid fee + energy tax + VAT
    double totalCostSum = 0.0;
    int    priceCount = 0;
    for (int i = 0; i < forecastData->count; i++)
    {
        if (forecastData->entries[i].valid)
        {
            time_t timestamp = forecastData->entries[i].timestamp;
            struct tm *tm_info = localtime(&timestamp);
            int hour = tm_info->tm_hour;

            double gridFee = GetGridFeeForHour(hour, gridFee_low, gridFee_normal, gridFee_high);
            double totalCost = CalculateTotalCost(forecastData->entries[i].spotPriceSek, gridFee, ENERGY_TAX_SEK_PER_KWH, VAT_RATE);

            totalCostSum += totalCost;
            priceCount++;
        }
    }
    double avgTotalCost = priceCount > 0 ? (totalCostSum / priceCount) : 1.0;
    double buyThreshold = avgTotalCost * BUY_PRICE_FRACTION;

    LOG_INFO("Compute: avg total cost=%.4f SEK/kWh (incl. grid fee, tax, VAT), buy threshold=%.4f", avgTotalCost, buyThreshold);

    // --- Pass 2: per-hour BUY / SELL / IDLE decision ---
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

        // TODO: Solar production model needs calibration.
        // Current formula: (W/m² / 1000) × area × efficiency × performance_ratio
        // Missing factors: panel tilt/azimuth correction, seasonal albedo,
        // module temperature coefficient, shading from surroundings.
        double irradiance  = fc->solarIrradiance / 1000.0; // W/m² → kW/m²
        double production  = irradiance * solarAreaM2 * solarEfficiency * PERFORMANCE_RATIO;

        double netKwh      = production - consumptionKwh;

        // Decision
        EnergyAction action;
        // TODO: Decision thresholds need proper calibration.
        // 0.05 kWh surplus threshold is arbitrary — should depend on inverter
        // minimum export limit and grid connection agreement.
        if (netKwh > 0.05)
        {
            action = ACTION_SELL_TO_GRID;
            totalExport += netKwh;
        }
        else if (fc->spotPriceSek < buyThreshold)
        {
            // TODO: BUY should also consider whether there is capacity to store
            // energy (battery) or a shiftable load available at this hour.
            action = ACTION_BUY_FROM_GRID;
        }
        else
        {
            action = ACTION_IDLE;
        }

        // Grid import and cost apply to all hours where consumption > production,
        // regardless of signal (BUY means "run extra loads", IDLE means "baseline only",
        // but both draw from the grid when netKwh < 0).
        if (netKwh < 0)
        {
            totalImport += (-netKwh);
            totalCost   += (-netKwh) * fc->spotPriceSek;
        }

        e->timestamp      = fc->timestamp;
        e->action         = action;
        e->productionKwh  = production;
        e->consumptionKwh = consumptionKwh;
        e->spotPrice      = fc->spotPriceSek;
        e->valid          = true;
    }

    plan->count              = count;
    plan->generatedAt        = time(NULL);
    plan->totalCostSek       = totalCost;
    plan->totalGridImportKwh = totalImport;
    plan->totalGridExportKwh = totalExport;

    LOG_INFO("Compute: Plan ready — %d entries, import=%.2f kWh, export=%.2f kWh, cost=%.2f SEK",
             count, totalImport, totalExport, totalCost);

    pthread_mutex_unlock(&compute->mutex);
    return 0;
}

void Compute_Shutdown(Compute *compute)
{
    if (!compute || !compute->isInitialized)
        return;

    pthread_mutex_destroy(&compute->mutex);
    compute->isInitialized = false;
    LOG_INFO("Compute: Shutdown");
}
