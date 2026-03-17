#ifndef _OPEN_METEO_DATA_H_
#define _OPEN_METEO_DATA_H_

typedef struct {
    char time[20];
    double temperature_2m;
    double humidity_2m;
    double cloud_cover;
    double wind_speed_10m;
    double shortwave_radiation;
} OpenMeteoEntry;

typedef struct {
    OpenMeteoEntry entries[192];  // 48h forecast: 192 quarters
    int count;
    char timezone[32];             // e.g., "Europe/Stockholm"
    int utc_offset_seconds;        // e.g., 3600 for CET, 7200 for CEST
} OpenMeteoResponse;

#endif
