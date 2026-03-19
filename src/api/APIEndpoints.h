#ifndef _API_ENDPOINTS_H_
#define _API_ENDPOINTS_H_

#include <time.h>

// Open-Meteo: temperature, humidity, cloud cover, wind speed, solar irradiance.
// buffer must be >= 512 bytes. Returns 0 on success, -1 on error.
int BuildOpenMeteoApiUrl(char *buffer, size_t bufferSize, const char *lat, const char *lon);

// Elpriset.se: Swedish spot prices per region (SE1-SE4).
// Pass date=NULL to use today. buffer must be >= 128 bytes.
// Returns 0 on success, -1 on error.
int BuildSpotPriceApiUrl(char *buffer, size_t bufferSize, const char *region, const struct tm *date);

// Same as above but always builds the URL for tomorrow's prices.
int BuildSpotPriceTomorrowUrl(char *buffer, size_t bufferSize, const char *region);

#endif // _API_ENDPOINTS_H_
