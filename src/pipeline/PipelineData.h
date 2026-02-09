#ifndef _PIPELINE_DATA_H_
#define _PIPELINE_DATA_H_

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
} PipelineEntry;

typedef struct {
    PipelineEntry entries[96];
    int count;
    time_t lastUpdated;
} PipelineData;

#endif
