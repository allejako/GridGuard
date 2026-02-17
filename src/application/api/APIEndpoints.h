#ifndef _API_ENDPOINTS_H_
#define _API_ENDPOINTS_H_

#include <time.h>

// Bygger URL för Open-Meteo forecast API
// Returnerar 0 vid success, -1 vid fel
// buffer måste vara minst 512 bytes
int BuildWeatherApiUrl(char *buffer, size_t bufferSize, const char *lat, const char *lon);

// Bygger URL för elprisetjustnu.se spotpris API
// Returnerar 0 vid success, -1 vid fel
// buffer måste vara minst 128 bytes
// Om date är NULL används dagens datum
int BuildSpotPriceApiUrl(char *buffer, size_t bufferSize, const char *region, const struct tm *date);

// Bygger URL för morgondagens spotpriser (publiceras ca kl 13:00)
int BuildSpotPriceTomorrowUrl(char *buffer, size_t bufferSize, const char *region);

#endif
