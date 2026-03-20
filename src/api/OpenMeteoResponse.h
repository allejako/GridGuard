#ifndef _OPEN_METEO_DATA_H_
#define _OPEN_METEO_DATA_H_

typedef struct
{
    char   time[20];
    double temperature2m;
    double humidity2m;
    double cloudCover;
    double windSpeed10m;
    double shortwaveRadiation;
} OpenMeteoEntry;

typedef struct
{
    OpenMeteoEntry entries[192];  // 48h forecast: 192 quarters
    int            count;
    char           timezone[32];       // e.g., "Europe/Stockholm"
    int            utcOffsetSeconds;   // e.g., 3600 for CET, 7200 for CEST
} OpenMeteoResponse;

#endif // _OPEN_METEO_DATA_H_
