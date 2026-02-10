#ifndef _COMPUTE_H_
#define _COMPUTE_H_

#include "ForecastData.h"
#include "EnergyData.h"
#include "OpenMeteoData.h"
#include "ElprisetData.h"

typedef struct
{
    SolarConfig solarConfig;
    BatteryConfig batteryConfig;
    ConsumptionProfile consumption;
    bool isInitialized;
} Compute;

int Compute_Initiate(Compute *compute, const SolarConfig *solarCfg, const BatteryConfig *batteryCfg, const ConsumptionProfile *consumptionCfg);
int Compute_CalculateSolarProduction(Compute *compute, const OpenMeteoResponse *forecast, SolarProduction *production, int maxEntries);
int Compute_GenerateEnergyPlan(Compute *compute, const OpenMeteoResponse *forecast, const ElprisetResponse *spotPrices, EnergyData *plan);
void Compute_Shutdown(Compute *compute);

#endif
