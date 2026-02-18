#ifndef _SOLAR_CONFIG_H_
#define _SOLAR_CONFIG_H_

typedef struct
{
    double panelEfficiency;       // 0-1, typically 0.15-0.22
    double panelAreaM2;
    double orientationDegrees;    // 0=north, 90=east, 180=south, 270=west
    double tiltDegrees;           // 0-90
    double peakPowerKw;
} SolarConfig;

#endif // _SOLAR_CONFIG_H_
