#ifndef PARSE_RESULT_H
#define PARSE_RESULT_H

#include "domain/Forecast.h"

// ParseResult: sent from Parser process → ComputeWorker thread via Unix domain socket.
typedef struct
{
    char   userId[64];
    char   location[64];
    char   region[16];
    double solarAreaM2;
    double solarEfficiency;
    double consumptionKwh;
    double gridFee_low;
    double gridFee_normal;
    double gridFee_high;
    ForecastData forecastData;
} ParseResult;

#endif // PARSE_RESULT_H
