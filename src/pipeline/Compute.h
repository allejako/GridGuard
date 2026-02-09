#ifndef _COMPUTE_H_
#define _COMPUTE_H_

#include "DataStructures.h"
#include "PipelineData.h"

typedef struct
{
    SolarConfig solarConfig;
    BatteryConfig batteryConfig;
    ConsumptionProfile consumption;
    bool isInitialized;
} Compute;

int Compute_Initiate(Compute *compute, const SolarConfig *solarCfg, const BatteryConfig *batteryCfg, const ConsumptionProfile *consumptionCfg);

// Legacy functions (kept for backwards compatibility) tillsvidare
int Compute_CalculateSolarProduction(Compute *compute, const WeatherForecast *forecast, SolarProduction *production, int maxEntries);
int Compute_GenerateEnergyPlan(Compute *compute, const WeatherForecast *forecast,const SpotPriceData *spotPrices, EnergyPlan *plan);

// New pipeline-based functions
int Compute_CalculateSolarFromPipeline(Compute *compute, const PipelineData *pipeline, SolarProduction *production, int maxEntries);
int Compute_GenerateEnergyPlanFromPipeline(Compute *compute, const PipelineData *pipeline, EnergyPlan *plan);

void Compute_Shutdown(Compute *compute);

#endif
