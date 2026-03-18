#ifndef _FORECAST_DATA_H_
#define _FORECAST_DATA_H_

#include <time.h>
#include <stdbool.h>

typedef struct {
    time_t timestamp;
    double solarIrradiance;   // W/m²
    double cloudCover;        // 0-100%
    double temperature;       // °C
    double windSpeed;         // m/s
    double humidity;          // 0-100%
    double spotPriceSek;      // SEK/kWh
    bool valid;
    bool hasPriceData;        // true if spotPriceSek is actual data (not default 0.0)
} ForecastEntry;

typedef struct {
    ForecastEntry entries[192];  // 48h forecast: 192 quarters (2 days × 96 quarters/day)
    int count;
    time_t lastUpdated;
} ForecastData;

#endif
