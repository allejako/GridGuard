#ifndef _PARSE_RESULT_H_
#define _PARSE_RESULT_H_

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
    double gridFeeLow;
    double gridFeeNormal;
    double gridFeeHigh;
    double panelTiltDeg;
    double panelAzimuthDeg;
    double latitudeDbl;
    double longitudeDbl;
    ForecastData forecastData;
} ParseResult;

#endif // _PARSE_RESULT_H_
