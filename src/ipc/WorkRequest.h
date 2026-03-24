#ifndef _WORK_REQUEST_H_
#define _WORK_REQUEST_H_

// WorkRequest: sent from HTTP handler → Fetcher process via anonymous pipe.
typedef struct
{
    char   userId[64];
    char   location[64];
    char   lat[16];
    char   lon[16];
    char   region[16];
    double solarAreaM2;
    double solarEfficiency;
    double consumptionKwh;
    double gridFeeLow;
    double gridFeeNormal;
    double gridFeeHigh;
    double panelTiltDeg;    // 0=horizontal, 90=vertical
    double panelAzimuthDeg; // compass bearing: 180=south (default)
    double latitudeDbl;     // precise latitude for solar position calculation
    double longitudeDbl;    // precise longitude for solar position calculation
} WorkRequest;

#endif // _WORK_REQUEST_H_
