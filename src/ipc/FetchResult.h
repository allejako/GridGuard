#ifndef _FETCH_RESULT_H_
#define _FETCH_RESULT_H_

// FetchResult: sent from Fetcher process → Parser process via named FIFO.
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
    char   openMeteoJson[32768];  // 48h weather data (~11KB with DNI+DHI for 192 quarters)
    char   priceJson[32768];      // 48h price data (today + tomorrow, ~26KB for 192 quarters)
} FetchResult;

#endif // _FETCH_RESULT_H_
