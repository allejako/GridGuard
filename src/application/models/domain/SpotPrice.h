#ifndef _SPOT_PRICE_H_
#define _SPOT_PRICE_H_

#include <time.h>
#include <stdbool.h>

typedef struct {
    time_t timestamp;
    double pricePerKwh;       // SEK/kWh
    char currency[4];         // "SEK", "EUR"
    char region[16];          // "SE3", "SE4"
    char source[32];          // "Elpriset.se", "Nordpool", "Tibber"
    bool valid;
} SpotPriceEntry;

typedef struct {
    SpotPriceEntry entries[96];
    int count;
    time_t lastUpdated;
    char primarySource[32]; 
} SpotPrice;

#endif // _SPOT_PRICE_H_
