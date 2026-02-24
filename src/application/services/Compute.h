#ifndef _COMPUTE_H_
#define _COMPUTE_H_

#include <pthread.h>
#include <stdbool.h>
#include "Energy.h"
#include "Forecast.h"

typedef struct
{
    bool            isInitialized;
    pthread_mutex_t mutex;
} Compute;

int  Compute_Initiate(Compute *compute);

// Generate BUY/SELL/IDLE plan for the per-user inputs.
//   solarAreaM2      – user's total panel area in m²
//   solarEfficiency  – panel efficiency 0–1 (e.g. 0.18)
//   consumptionKwh   – user's average hourly base load (kWh/h)
int  Compute_GenerateEnergyPlan(Compute *compute,
                                const ForecastData *forecastData,
                                double solarAreaM2,
                                double solarEfficiency,
                                double consumptionKwh,
                                EnergyData *plan);

void Compute_Shutdown(Compute *compute);

#endif // _COMPUTE_H_
