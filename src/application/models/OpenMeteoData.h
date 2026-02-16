#ifndef _OPEN_METEO_DATA_H_
#define _OPEN_METEO_DATA_H_

typedef struct {
    char time[20];              // "2026-02-09T00:00"
    double temperature_2m;      // °C
    double humidity_2m;         // %
    double cloud_cover;         // %
    double wind_speed_10m;      // km/h
    double shortwave_radiation; // W/m²
} OpenMeteoEntry;

typedef struct {
    OpenMeteoEntry entries[96]; // 4 dagar x 24 timmar = 96 datapunkter
    int count; // Antal giltiga datapunkter
} OpenMeteoResponse;

#endif
