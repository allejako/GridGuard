#ifndef FETCH_RESULT_H
#define FETCH_RESULT_H

// FetchResult: sent from Fetcher process → Parser process via named FIFO.
typedef struct
{
    char   userId[64];
    char   location[64];
    char   region[16];
    double solarAreaM2;
    double solarEfficiency;
    double consumptionKwh;
    double gridFee_low;
    double gridFee_normal;
    double gridFee_high;
    char   openMeteoJson[32768];
    char   priceJson[16384];
} FetchResult;

#endif // FETCH_RESULT_H
