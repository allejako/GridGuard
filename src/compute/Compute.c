#include "compute/Compute.h"
#include "sys/Logger.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

// IEC 61724 performance ratio — wiring losses and inverter efficiency.
#define PERF_RATIO       0.75

// Minimum net solar surplus to trigger a SELL signal.
// Below this, inverter switching losses exceed the gain.
#define SELL_MIN_KWH     0.05

// Minimum spot price to justify exporting. Negative prices mean the grid
// charges producers; near-zero rarely yields meaningful feed-in revenue.
#define SELL_MIN_PRICE   0.05

// NOCT model for crystalline silicon (IEC 61215).
#define NOCT             45.0
#define TEMP_STC         25.0
#define TEMP_COEFF       (-0.0045)  // power loss per °C above STC
#define WIND_COOL        0.04       // convective cooling factor

// Swedish fixed cost components.
#define ENERGY_TAX       0.40  // SEK/kWh (energiskatt)
#define VAT              0.25

// BUY threshold: cheapest 30% of hours in the forecast window.
#define BUY_PERCENTILE   0.30

// BUY quality gate: the threshold must be at least this far below median.
// Prevents false BUY signals on flat-price days where the "cheap" hours
// are barely cheaper than average and shifting load isn't worth the effort.
#define BUY_MIN_DISCOUNT 0.10

// AVOID threshold: most expensive 30% of hours in the forecast window.
#define AVOID_PERCENTILE 0.70

// AVOID quality gate: the threshold must be at least this far above median.
// Prevents false AVOID signals on flat-price days where the "expensive" hours
// are barely more expensive than average.
#define AVOID_MIN_PREMIUM 0.10

static double grid_fee(int hour, double low, double normal, double high)
{
    if (hour < 7)  return low;
    if (hour < 17) return normal;
    return high;
}

// Cell temperature from ambient conditions using the NOCT model.
// Wind reduces the temperature rise above ambient by increasing convective cooling.
static double cell_temp(double ambient, double irradiance, double wind)
{
    return ambient + (NOCT - 20.0) / (800.0 * (1.0 + WIND_COOL * wind)) * irradiance;
}

// Typical Swedish household load profile relative to the configured average.
// Night (00-06):   low — sleeping, almost nothing running.
// Day (07-16):     moderate — away at work, background appliances.
// Evening (17-22): high — cooking, heating, EV charging, TV.
// Late (23):       tapering — last lights, dishwasher finishing.
static double consumption_factor(int hour)
{
    if (hour < 7)  return 0.40;
    if (hour < 17) return 1.00;
    if (hour < 23) return 1.60;
    return 0.70;
}

static int cmp_double(const void *a, const void *b)
{
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

int Compute_Initiate(Compute *compute)
{
    if (!compute) return -1;
    memset(compute, 0, sizeof(Compute));
    pthread_mutex_init(&compute->mutex, NULL);
    compute->isInitialized = true;
    LOG_INFO("Compute: ready");
    return 0;
}

int Compute_GenerateEnergyPlan(Compute *compute,
                                const ForecastData *forecast,
                                double solarAreaM2,
                                double solarEfficiency,
                                double consumptionKwh,
                                double gridFee_low,
                                double gridFee_normal,
                                double gridFee_high,
                                EnergyData *plan)
{
    if (!compute || !compute->isInitialized || !forecast || !plan)
        return -1;

    pthread_mutex_lock(&compute->mutex);

    int n = forecast->count;
    if (n <= 0)
    {
        LOG_ERROR("Compute: empty forecast");
        pthread_mutex_unlock(&compute->mutex);
        return -1;
    }

    // Pre-compute the full consumer cost per hour: spot price + time-of-use grid
    // fee + energy tax + VAT. Collect valid values for the percentile calculation.
    double costs[96] = {0.0};
    double sorted[96];
    int    validCount = 0;

    for (int i = 0; i < n; i++)
    {
        const ForecastEntry *fc = &forecast->entries[i];
        if (!fc->valid) continue;

        struct tm *t = localtime(&fc->timestamp);
        int hour = t ? t->tm_hour : 12;

        double fee  = grid_fee(hour, gridFee_low, gridFee_normal, gridFee_high);
        double cost = (fc->spotPriceSek + fee + ENERGY_TAX) * (1.0 + VAT);

        costs[i]             = cost;
        sorted[validCount++] = cost;
    }

    if (validCount == 0)
    {
        LOG_ERROR("Compute: no valid forecast entries");
        pthread_mutex_unlock(&compute->mutex);
        return -1;
    }

    qsort(sorted, validCount, sizeof(double), cmp_double);

    int p30 = (int)(validCount * BUY_PERCENTILE);
    if (p30 >= validCount) p30 = validCount - 1;
    int p50 = validCount / 2;
    int p70 = (int)(validCount * AVOID_PERCENTILE);
    if (p70 >= validCount) p70 = validCount - 1;

    double buyThreshold   = sorted[p30];
    double median         = sorted[p50];
    double avoidThreshold = sorted[p70];

    // Quality gate: only signal BUY if the price is meaningfully below median.
    // On flat-price days the p30 threshold may be nearly identical to median,
    // making the signal useless — don't cry wolf.
    double buyQualityCap = median * (1.0 - BUY_MIN_DISCOUNT);
    if (buyThreshold > buyQualityCap)
        buyThreshold = buyQualityCap;

    // Quality gate: only signal AVOID if the price is meaningfully above median.
    double avoidQualityFloor = median * (1.0 + AVOID_MIN_PREMIUM);
    if (avoidThreshold < avoidQualityFloor)
        avoidThreshold = avoidQualityFloor;

    LOG_INFO("Compute: p30=%.4f  median=%.4f  p70=%.4f  max=%.4f  buy=%.4f  avoid=%.4f SEK/kWh",
             sorted[p30], median, sorted[p70], sorted[validCount - 1], buyThreshold, avoidThreshold);

    memset(plan, 0, sizeof(EnergyData));

    double totalImport = 0.0, totalExport = 0.0, totalCost = 0.0;

    for (int i = 0; i < n; i++)
    {
        const ForecastEntry *fc = &forecast->entries[i];
        EnergyDataEntry     *e  = &plan->entries[i];
        if (!fc->valid) continue;

        struct tm *t    = localtime(&fc->timestamp);
        int        hour = t ? t->tm_hour : 12;

        // Solar production with NOCT temperature correction.
        // tempFactor > 1 on cold days (bonus), < 1 on hot days (derating).
        // Clamped to [0.70, 1.10] to guard against implausible sensor readings.
        double tc  = cell_temp(fc->temperature, fc->solarIrradiance, fc->windSpeed);
        double df  = 1.0 + TEMP_COEFF * (tc - TEMP_STC);
        if (df < 0.70) df = 0.70;
        if (df > 1.10) df = 1.10;

        double prod       = (fc->solarIrradiance / 1000.0) * solarAreaM2 * solarEfficiency * PERF_RATIO * df;
        double hourlyLoad = consumptionKwh * consumption_factor(hour);
        double net        = prod - hourlyLoad;
        double cost       = costs[i];

        EnergyAction action;

        if (net > SELL_MIN_KWH && fc->spotPriceSek >= SELL_MIN_PRICE)
        {
            action = ACTION_SELL_TO_GRID;
            totalExport += net;
        }
        else if (cost <= buyThreshold)
        {
            action = ACTION_BUY_FROM_GRID;
        }
        else if (cost >= avoidThreshold)
        {
            action = ACTION_AVOID_HIGH_PRICE;
        }
        else
        {
            action = ACTION_IDLE;
        }

        if (net < 0.0)
        {
            totalImport += -net;
            totalCost   += -net * cost;
        }

        e->timestamp          = fc->timestamp;
        e->action             = action;
        e->productionKwh      = prod;
        e->consumptionKwh     = hourlyLoad;
        e->spotPrice          = fc->spotPriceSek;
        e->totalCostSek       = cost;
        e->priceVsAvgPct      = median > 0.0 ? (cost - median) / median * 100.0 : 0.0;
        e->valid              = true;
    }

    plan->count              = n;
    plan->generatedAt        = time(NULL);
    plan->totalCostSek       = totalCost;
    plan->totalGridImportKwh = totalImport;
    plan->totalGridExportKwh = totalExport;

    // Find the best contiguous BUY window across the entire forecast.
    // savingsSek = sum of (savings/kWh × consumption) per hour — actual SEK
    // the customer saves by shifting their load into this window vs peak hours.
    {
        int    wStart    = -1, wLast = -1;
        double wCost     = 0.0, wSavings = 0.0;
        int    wHours    = 0;
        double bestSavings = -1.0;

        for (int i = 0; i <= n; i++)
        {
            int isBuy = (i < n
                         && plan->entries[i].valid
                         && plan->entries[i].action == ACTION_BUY_FROM_GRID);

            if (isBuy)
            {
                if (wStart < 0) wStart = i;
                wLast     = i;
                wCost    += plan->entries[i].totalCostSek;
                wSavings += (median - plan->entries[i].totalCostSek) * plan->entries[i].consumptionKwh;
                wHours++;
            }
            else if (wStart >= 0)
            {
                if (wSavings > bestSavings)
                {
                    bestSavings                    = wSavings;
                    plan->bestBuyWindow.start      = plan->entries[wStart].timestamp;
                    plan->bestBuyWindow.end        = plan->entries[wLast].timestamp;
                    plan->bestBuyWindow.hours      = wHours;
                    plan->bestBuyWindow.avgCostSek = wCost / wHours;
                    plan->bestBuyWindow.savingsSek = wSavings;
                    plan->hasBuyWindow             = 1;
                }
                wStart = -1; wLast = -1;
                wCost = 0.0; wSavings = 0.0; wHours = 0;
            }
        }
    }

    if (plan->hasBuyWindow)
        LOG_INFO("Compute: best BUY window — %dh at %.4f SEK/kWh avg, saves %.2f SEK",
                 plan->bestBuyWindow.hours,
                 plan->bestBuyWindow.avgCostSek,
                 plan->bestBuyWindow.savingsSek);
    else
        LOG_INFO("Compute: no BUY window found (flat price or all solar)");

    LOG_INFO("Compute: %d entries — import %.2f kWh, export %.2f kWh, cost %.2f SEK",
             n, totalImport, totalExport, totalCost);

    pthread_mutex_unlock(&compute->mutex);
    return 0;
}

void Compute_Shutdown(Compute *compute)
{
    if (!compute || !compute->isInitialized) return;
    pthread_mutex_destroy(&compute->mutex);
    compute->isInitialized = false;
    LOG_INFO("Compute: shutdown");
}
