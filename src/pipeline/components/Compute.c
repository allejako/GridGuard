// MOCK IMPLEMENTATION - Simplified single-threaded computation with mock data
// Not yet fully implemented - generates simple mock data to demonstrate pipeline flow

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "Compute.h"
#include "Logger.h"

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

int Compute_GenerateEnergyPlan(Compute *compute,
                              const OpenMeteoResponse *forecast,
                              const ElprisetResponse *spotPrices,
                              EnergyData *plan)
{
    if (!compute || !compute->isInitialized || !forecast || !spotPrices || !plan)
        return -1;

    LOG_INFO("Generating mock energy plan");

    // Generate mock solar production
    SolarProduction solarProd[288];
    int prodCount = Compute_CalculateSolarProduction(compute, forecast, solarProd, 288);
    if (prodCount <= 0)
    {
        LOG_ERROR("No solar production data");
        return -1;
    }

    // Generate simple mock energy plan
    int planCount = prodCount < spotPrices->count ? prodCount : spotPrices->count;
    double mockBatterySoc = 50.0; // mock battery state

    for (int i = 0; i < planCount; i++)
    {
        EnergyDataEntry *entry = &plan->entries[i];
        const SolarProduction *prod = &solarProd[i];
        const ElprisetEntry *price = &spotPrices->entries[i];

        // Simple mock data
        entry->timestamp = prod->timestamp;
        entry->productionKwh = prod->productionKwh;
        entry->spotPrice = price->SEK_per_kWh;
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

    LOG_INFO("Generated mock energy plan: %d entries", planCount);
    return 0;
}

void Compute_Shutdown(Compute *compute)
{
    if (!compute || !compute->isInitialized)
        return;

    compute->isInitialized = false;
    LOG_INFO("Compute shutdown");
}
