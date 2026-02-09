#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "Compute.h"
#include "Logger.h"

int Compute_Initiate(Compute *compute, const SolarConfig *solarCfg, const BatteryConfig *batteryCfg, const ConsumptionProfile *consumptionCfg)
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

int Compute_CalculateSolar(Compute *compute, const ForecastData *forecast, SolarProduction *production, int maxEntries)
{
    if (!compute || !compute->isInitialized || !forecast || !production)
        return -1;

    int count = 0;
    for (int i = 0; i < forecast->count && count < maxEntries; i++)
    {
        const ForecastEntry *entry = &forecast->entries[i];
        if (!entry->valid)
            continue;

        SolarProduction *prod = &production[count];
        prod->timestamp = entry->timestamp;

        double area = compute->solarConfig.panelAreaM2;
        double efficiency = compute->solarConfig.panelEfficiency;
        double irradiance = entry->solarIrradiance / 1000.0;
        double performanceRatio = 0.75;

        double tempCoeff = 1.0 - (0.005 * (entry->temperature - 25.0));
        if (tempCoeff < 0.5) tempCoeff = 0.5;
        if (tempCoeff > 1.2) tempCoeff = 1.2;

        prod->productionKwh = area * efficiency * irradiance * performanceRatio * tempCoeff;
        prod->efficiencyFactor = efficiency * performanceRatio * tempCoeff;
        prod->valid = true;

        count++;
    }

    LOG_INFO("Calculated %d solar production forecasts from forecast", count);
    return count;
}

int Compute_GenerateEnergyPlan(Compute *compute, const ForecastData *forecast, EnergyPlan *plan)
{
    if (!compute || !compute->isInitialized || !forecast || !plan)
        return -1;

    SolarProduction solarProd[288];
    int prodCount = Compute_CalculateSolar(compute, forecast, solarProd, 288);
    if (prodCount <= 0)
    {
        LOG_ERROR("No solar production data from forecast");
        return -1;
    }

    double batterySoc = compute->batteryConfig.currentSocPercent;
    double totalCost = 0.0;
    double totalImport = 0.0;
    double totalExport = 0.0;

    int planCount = prodCount < forecast->count ? prodCount : forecast->count;

    for (int i = 0; i < planCount; i++)
    {
        EnergyPlanEntry *e = &plan->entries[i];
        const SolarProduction *prod = &solarProd[i];
        const ForecastEntry *pe = &forecast->entries[i];

        e->timestamp = prod->timestamp;
        e->productionKwh = prod->productionKwh;
        e->spotPrice = pe->spotPriceSek;
        e->consumptionKwh = compute->consumption.baseLoadKw;

        double surplus = e->productionKwh - e->consumptionKwh;
        double priceThreshold = 1.0;

        if (surplus > 0)
        {
            if (pe->spotPriceSek > priceThreshold)
            {
                e->action = ACTION_SELL_TO_GRID;
                e->gridPowerKwh = surplus;
                e->batteryPowerKwh = 0;
                totalExport += surplus;
                totalCost -= surplus * pe->spotPriceSek;
            }
            else if (batterySoc < compute->batteryConfig.maxSocPercent)
            {
                double chargeAmount = surplus;
                double maxCharge = compute->batteryConfig.maxChargeRateKw;
                if (chargeAmount > maxCharge)
                    chargeAmount = maxCharge;

                e->action = ACTION_CHARGE_BATTERY;
                e->batteryPowerKwh = chargeAmount;
                e->gridPowerKwh = surplus - chargeAmount;
                batterySoc += (chargeAmount / compute->batteryConfig.capacityKwh) * 100.0;
            }
            else
            {
                e->action = ACTION_DIRECT_USE;
                e->gridPowerKwh = 0;
                e->batteryPowerKwh = 0;
            }
        }
        else
        {
            double deficit = -surplus;

            if (pe->spotPriceSek > priceThreshold &&
                batterySoc > compute->batteryConfig.minSocPercent)
            {
                double dischargeAmount = deficit;
                double maxDischarge = compute->batteryConfig.maxDischargeRateKw;
                if (dischargeAmount > maxDischarge)
                    dischargeAmount = maxDischarge;

                e->action = ACTION_DISCHARGE_BATTERY;
                e->batteryPowerKwh = -dischargeAmount;
                e->gridPowerKwh = deficit - dischargeAmount;
                batterySoc -= (dischargeAmount / compute->batteryConfig.capacityKwh) * 100.0;
            }
            else
            {
                e->action = ACTION_BUY_FROM_GRID;
                e->gridPowerKwh = deficit;
                e->batteryPowerKwh = 0;
                totalImport += deficit;
                totalCost += deficit * pe->spotPriceSek;
            }
        }

        e->batterySocPercent = batterySoc;
        e->estimatedCostSek = e->gridPowerKwh * pe->spotPriceSek;
        e->valid = true;
    }

    plan->count = planCount;
    plan->generatedAt = time(NULL);
    plan->totalCostSek = totalCost;
    plan->totalGridImportKwh = totalImport;
    plan->totalGridExportKwh = totalExport;

    LOG_INFO("Generated energy plan from forecast: %d entries, cost: %.2f SEK", planCount, totalCost);
    return 0;
}

void Compute_Shutdown(Compute *compute)
{
    if (!compute || !compute->isInitialized)
        return;

    compute->isInitialized = false;
    LOG_INFO("Compute shutdown");
}
