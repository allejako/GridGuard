#ifndef _COMPUTE_H_
#define _COMPUTE_H_

#include <pthread.h>
#include <stdbool.h>
#include "domain/Energy.h"
#include "domain/Forecast.h"

typedef struct
{
    bool isInitialized;
    pthread_mutex_t mutex;
} Compute;

int Compute_Initiate(Compute *compute);

// Generate a BUY/SELL/AVOID/IDLE plan from forecast data and user parameters.
// Grid fees are split into three time bands: low (night), normal (day), high (peak).
// panelTiltDeg: 0=horizontal, 90=vertical (30-40° typical for Sweden)
// panelAzimuthDeg: compass bearing, 180=south (optimal for Sweden)
// latitude/longitude: used for solar position calculation (Hay & Davies POA model)
int Compute_GenerateEnergyPlan(Compute *compute, const ForecastData *forecastData, double solarAreaM2, double solarEfficiency, double consumptionKwh, double gridFeeLow, double gridFeeNormal, double gridFeeHigh, double panelTiltDeg, double panelAzimuthDeg, double latitude, double longitude, EnergyData *plan);

void Compute_Shutdown(Compute *compute);

#endif // _COMPUTE_H_
