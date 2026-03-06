#ifndef WORK_REQUEST_H
#define WORK_REQUEST_H

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
    double gridFee_low;
    double gridFee_normal;
    double gridFee_high;
} WorkRequest;

#endif // WORK_REQUEST_H
