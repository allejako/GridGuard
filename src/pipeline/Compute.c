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
                                     const WeatherForecast *forecast,
                                     SolarProduction *production,
                                     int maxEntries)
{
    if (!compute || !compute->isInitialized || !forecast || !production)
        return -1;

    int count = 0;
    for (int i = 0; i < forecast->count && count < maxEntries; i++)
    {
        const WeatherData *weather = &forecast->forecasts[i];
        if (!weather->valid)
            continue;

        SolarProduction *prod = &production[count];
        prod->timestamp = weather->timestamp;

        // P = A * r * H * PR
        double area = compute->solarConfig.panelAreaM2;
        double efficiency = compute->solarConfig.panelEfficiency;
        double irradiance = weather->solarIrradiance / 1000.0; // W/m² -> kW/m²
        double performanceRatio = 0.75;

        // Temperaturkorrigering (~0.5% forlust per grad over 25C)
        double tempCoeff = 1.0 - (0.005 * (weather->temperature - 25.0));
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
                              const WeatherForecast *forecast,
                              const SpotPriceData *spotPrices,
                              EnergyPlan *plan)
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
        EnergyPlanEntry *entry = &plan->entries[i];
        const SolarProduction *prod = &solarProd[i];
        const SpotPrice *price = &spotPrices->prices[i];

        entry->timestamp = prod->timestamp;
        entry->productionKwh = prod->productionKwh;
        entry->spotPrice = price->priceSekPerKwh;
        entry->consumptionKwh = compute->consumption.baseLoadKw;

        double surplus = entry->productionKwh - entry->consumptionKwh;
        double priceThreshold = 1.0; // SEK/kWh

        if (surplus > 0)
        {
            if (price->priceSekPerKwh > priceThreshold)
            {
                entry->action = ACTION_SELL_TO_GRID;
                entry->gridPowerKwh = surplus;
                entry->batteryPowerKwh = 0;
                totalExport += surplus;
                totalCost -= surplus * price->priceSekPerKwh;
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

            if (price->priceSekPerKwh > priceThreshold &&
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
                totalCost += deficit * price->priceSekPerKwh;
            }
        }

        entry->batterySocPercent = batterySoc;
        entry->estimatedCostSek = entry->gridPowerKwh * price->priceSekPerKwh;
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
