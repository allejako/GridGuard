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
//   gridFee_low      – night rate (00:00-06:59) in kr/kWh
//   gridFee_normal   – day rate (07:00-16:59) in kr/kWh
//   gridFee_high     – peak rate (17:00-23:59) in kr/kWh
int  Compute_GenerateEnergyPlan(Compute *compute,
                                const ForecastData *forecastData,
                                double solarAreaM2,
                                double solarEfficiency,
                                double consumptionKwh,
                                double gridFee_low,
                                double gridFee_normal,
                                double gridFee_high,
                                EnergyData *plan);

void Compute_Shutdown(Compute *compute);

#endif // _COMPUTE_H_
