#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "Compute.h"
#include "Logger.h"

int Compute_Initiate(Compute *compute, const SolarConfig *solarCfg,
                     const BatteryConfig *batteryCfg,
                     const ConsumptionProfile *consumptionCfg)
{
    if (!compute || !solarCfg || !batteryCfg || !consumptionCfg)
        return -1;

    compute->solarConfig = *solarCfg;
    compute->batteryConfig = *batteryCfg;
    compute->consumption = *consumptionCfg;
    compute->isInitialized = true;

    LOG_INFO("Compute initialized");
    return 0;
}

int Compute_CalculateSolarProduction(Compute *compute,
                                     const OpenMeteoResponse *forecast,
                                     SolarProduction *production,
                                     int maxEntries)
{
    if (!compute || !compute->isInitialized || !forecast || !production)
        return -1;

    int count = 0;
    for (int i = 0; i < forecast->count && count < maxEntries; i++)
    {
        const OpenMeteoEntry *weather = &forecast->entries[i];

        SolarProduction *prod = &production[count];

        // Convert time string to timestamp (simplified - just use index for now)
        prod->timestamp = 0; // TODO: Parse time string to timestamp

        // P = A * r * H * PR
        double area = compute->solarConfig.panelAreaM2;
        double efficiency = compute->solarConfig.panelEfficiency;
        double irradiance = weather->shortwave_radiation / 1000.0; // W/m² -> kW/m²
        double performanceRatio = 0.75;

        // Temperaturkorrigering (~0.5% forlust per grad over 25C)
        double tempCoeff = 1.0 - (0.005 * (weather->temperature_2m - 25.0));
        if (tempCoeff < 0.5) tempCoeff = 0.5;
        if (tempCoeff > 1.2) tempCoeff = 1.2;

        // Produktion per timme
        prod->productionKwh = area * efficiency * irradiance * performanceRatio * tempCoeff;
        prod->efficiencyFactor = efficiency * performanceRatio * tempCoeff;
        prod->valid = true;

        count++;
    }

    LOG_INFO("Calculated %d solar production forecasts", count);
    return count;
}

int Compute_GenerateEnergyPlan(Compute *compute,
                              const OpenMeteoResponse *forecast,
                              const ElprisetResponse *spotPrices,
                              EnergyData *plan)
{
    if (!compute || !compute->isInitialized || !forecast || !spotPrices || !plan)
        return -1;

    SolarProduction solarProd[288];
    int prodCount = Compute_CalculateSolarProduction(compute, forecast, solarProd, 288);
    if (prodCount <= 0)
    {
        LOG_ERROR("No solar production data");
        return -1;
    }

    double batterySoc = compute->batteryConfig.currentSocPercent;
    double totalCost = 0.0;
    double totalImport = 0.0;
    double totalExport = 0.0;

    int planCount = prodCount < spotPrices->count ? prodCount : spotPrices->count;

    for (int i = 0; i < planCount; i++)
    {
        EnergyDataEntry *entry = &plan->entries[i];
        const SolarProduction *prod = &solarProd[i];
        const ElprisetEntry *price = &spotPrices->entries[i];

        entry->timestamp = prod->timestamp;
        entry->productionKwh = prod->productionKwh;
        entry->spotPrice = price->SEK_per_kWh;
        entry->consumptionKwh = compute->consumption.baseLoadKw;

        double surplus = entry->productionKwh - entry->consumptionKwh;
        double priceThreshold = 1.0; // SEK/kWh

        if (surplus > 0)
        {
            if (price->SEK_per_kWh > priceThreshold)
            {
                entry->action = ACTION_SELL_TO_GRID;
                entry->gridPowerKwh = surplus;
                entry->batteryPowerKwh = 0;
                totalExport += surplus;
                totalCost -= surplus * price->SEK_per_kWh;
            }
            else if (batterySoc < compute->batteryConfig.maxSocPercent)
            {
                double chargeAmount = surplus;
                double maxCharge = compute->batteryConfig.maxChargeRateKw;
                if (chargeAmount > maxCharge)
                    chargeAmount = maxCharge;

                entry->action = ACTION_CHARGE_BATTERY;
                entry->batteryPowerKwh = chargeAmount;
                entry->gridPowerKwh = surplus - chargeAmount;
                batterySoc += (chargeAmount / compute->batteryConfig.capacityKwh) * 100.0;
            }
            else
            {
                entry->action = ACTION_DIRECT_USE;
                entry->gridPowerKwh = 0;
                entry->batteryPowerKwh = 0;
            }
        }
        else
        {
            double deficit = -surplus;

            if (price->SEK_per_kWh > priceThreshold &&
                batterySoc > compute->batteryConfig.minSocPercent)
            {
                double dischargeAmount = deficit;
                double maxDischarge = compute->batteryConfig.maxDischargeRateKw;
                if (dischargeAmount > maxDischarge)
                    dischargeAmount = maxDischarge;

                entry->action = ACTION_DISCHARGE_BATTERY;
                entry->batteryPowerKwh = -dischargeAmount;
                entry->gridPowerKwh = deficit - dischargeAmount;
                batterySoc -= (dischargeAmount / compute->batteryConfig.capacityKwh) * 100.0;
            }
            else
            {
                entry->action = ACTION_BUY_FROM_GRID;
                entry->gridPowerKwh = deficit;
                entry->batteryPowerKwh = 0;
                totalImport += deficit;
                totalCost += deficit * price->SEK_per_kWh;
            }
        }

        entry->batterySocPercent = batterySoc;
        entry->estimatedCostSek = entry->gridPowerKwh * price->SEK_per_kWh;
        entry->valid = true;
    }

    plan->count = planCount;
    plan->generatedAt = time(NULL);
    plan->totalCostSek = totalCost;
    plan->totalGridImportKwh = totalImport;
    plan->totalGridExportKwh = totalExport;

    LOG_INFO("Generated energy plan: %d entries, cost: %.2f SEK", planCount, totalCost);
    return 0;
}

void Compute_Shutdown(Compute *compute)
{
    if (!compute || !compute->isInitialized)
        return;

    compute->isInitialized = false;
    LOG_INFO("Compute shutdown");
}
