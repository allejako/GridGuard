#ifndef _COMPUTE_H_
#define _COMPUTE_H_

#include "EnergyPlan.h"
#include "ForecastData.h"

typedef struct
{
    SolarConfig solarConfig;
    BatteryConfig batteryConfig;
    ConsumptionProfile consumption;
    bool isInitialized;
} Compute;

int Compute_Initiate(Compute *compute, const SolarConfig *solarCfg, const BatteryConfig *batteryCfg, const ConsumptionProfile *consumptionCfg);

int Compute_CalculateSolar(Compute *compute, const ForecastData *forecast, SolarProduction *production, int maxEntries);
int Compute_GenerateEnergyPlan(Compute *compute, const ForecastData *forecast, EnergyPlan *plan);

void Compute_Shutdown(Compute *compute);

#endif
