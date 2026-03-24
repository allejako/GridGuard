#ifndef _USER_CONFIG_H_
#define _USER_CONFIG_H_

typedef struct
{
    char   userId[128];
    char   location[64];    // user-friendly display name/city
    double latitude;
    double longitude;
    char   region[16];
    double solarAreaM2;     // m², user's total panel area
    double solarEfficiency; // 0–1, panel efficiency (e.g. 0.18)
    double consumptionKwh;  // average hourly base load (kWh/h)

    // Grid fees (kr/kWh) - time-of-use tariffs
    double gridFeeLow;      // 00:00-06:59 (night rate)
    double gridFeeNormal;   // 07:00-16:59 (day rate)
    double gridFeeHigh;     // 17:00-23:59 (peak rate)

    // Solar panel orientation — needed for accurate POA irradiance calculation
    double panelTiltDeg;    // 0=horizontal, 90=vertical (typical Swedish roof: 30-40°)
    double panelAzimuthDeg; // compass bearing: 0=north, 90=east, 180=south, 270=west

    long updatedAt;
} UserConfig;

#endif // _USER_CONFIG_H_
