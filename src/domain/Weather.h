#ifndef _WEATHER_DATA_H_
#define _WEATHER_DATA_H_

#include <time.h>
#include <stdbool.h>

typedef struct
{
    time_t timestamp;
    double temperature;
    double humidity;
    double cloudCover;
    double windSpeed;
    double solarIrradiance;
    bool   valid;
} WeatherEntry;

typedef struct
{
    WeatherEntry entries[96];
    int    count;
    time_t lastUpdated;
} WeatherData;

#endif // _WEATHER_DATA_H_
