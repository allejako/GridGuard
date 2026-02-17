// MOCK IMPLEMENTATION - Simplified single-threaded computation with mock data
// Not yet fully implemented - generates simple mock data to demonstrate pipeline flow

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "Compute.h"
#include "Logger.h"

// Internal unlocked variant - called by other Compute functions that already hold the mutex
static int CalculateSolarProduction_Internal(Compute *compute,
                                              const OpenMeteoResponse *forecast,
                                              SolarProduction *production,
                                              int maxEntries)
{
    LOG_INFO("Calculating solar production (MOCK DATA)");

    int count = forecast->count < maxEntries ? forecast->count : maxEntries;

    // Generate mock solar production data
    for (int i = 0; i < count; i++)
    {
        SolarProduction *prod = &production[i];
        const OpenMeteoEntry *weather = &forecast->entries[i];

        // Simple mock calculation
        double irradiance = weather->shortwave_radiation / 1000.0;
        prod->productionKwh = compute->solarConfig.panelAreaM2 *
                             compute->solarConfig.panelEfficiency *
                             irradiance * 0.75; // mock performance ratio
        prod->efficiencyFactor = 0.75;
        prod->timestamp = 0; // mock timestamp
        prod->valid = true;
    }

    LOG_INFO("Calculated %d solar production forecasts (MOCK)", count);
    return count;
}

int Compute_Initiate(Compute *compute, const SolarConfig *solarCfg,
                     const BatteryConfig *batteryCfg,
                     const ConsumptionProfile *consumptionCfg)
{
    if (!compute || !solarCfg || !batteryCfg || !consumptionCfg)
        return -1;

    memset(compute, 0, sizeof(Compute));

    compute->solarConfig = *solarCfg;
    compute->batteryConfig = *batteryCfg;
    compute->consumption = *consumptionCfg;
    compute->isInitialized = true;
    pthread_mutex_init(&compute->mutex, NULL);

    LOG_INFO("Compute initialized (MOCK MODE)");
    return 0;
}

int Compute_CalculateSolarProduction(Compute *compute,
                                     const OpenMeteoResponse *forecast,
                                     SolarProduction *production,
                                     int maxEntries)
{
    if (!compute || !compute->isInitialized || !forecast || !production)
        return -1;

    pthread_mutex_lock(&compute->mutex);
    int result = CalculateSolarProduction_Internal(compute, forecast, production, maxEntries);
    pthread_mutex_unlock(&compute->mutex);
    return result;
}

int Compute_GenerateEnergyPlan(Compute *compute,
                              const ForecastData *forecastData,
                              EnergyData *plan)
{
    if (!compute || !compute->isInitialized || !forecastData || !plan)
        return -1;

    pthread_mutex_lock(&compute->mutex);

    LOG_INFO("Generating mock energy plan from unified forecast data");

    if (forecastData->count <= 0)
    {
        LOG_ERROR("No forecast data available");
        pthread_mutex_unlock(&compute->mutex);
        return -1;
    }

    // Generate simple mock energy plan from unified forecast
    int planCount = forecastData->count;
    double mockBatterySoc = 50.0; // mock battery state

    for (int i = 0; i < planCount; i++)
    {
        EnergyDataEntry *entry = &plan->entries[i];
        const ForecastEntry *forecast = &forecastData->entries[i];

        if (!forecast->valid)
            continue;

        // Calculate simple mock solar production
        double irradiance = forecast->solarIrradiance / 1000.0;
        double production = compute->solarConfig.panelAreaM2 *
                           compute->solarConfig.panelEfficiency *
                           irradiance * 0.75; // mock performance ratio

        // Fill energy plan entry
        entry->timestamp = forecast->timestamp;
        entry->productionKwh = production;
        entry->spotPrice = forecast->spotPriceSek;
        entry->consumptionKwh = compute->consumption.baseLoadKw;
        entry->batterySocPercent = mockBatterySoc;
        entry->gridPowerKwh = 0.0;
        entry->batteryPowerKwh = 0.0;
        entry->estimatedCostSek = 0.0;
        entry->action = ACTION_DIRECT_USE; // mock action
        entry->valid = true;
    }

    plan->count = planCount;
    plan->generatedAt = time(NULL);
    plan->totalCostSek = 0.0; // mock
    plan->totalGridImportKwh = 0.0; // mock
    plan->totalGridExportKwh = 0.0; // mock
    plan->totalBatteryCycles = 0.0; // mock

    LOG_INFO("Generated mock energy plan: %d entries from unified forecast", planCount);
    pthread_mutex_unlock(&compute->mutex);
    return 0;
}

void Compute_Shutdown(Compute *compute)
{
    if (!compute || !compute->isInitialized)
        return;

    pthread_mutex_destroy(&compute->mutex);
    compute->isInitialized = false;
    LOG_INFO("Compute shutdown");
}
