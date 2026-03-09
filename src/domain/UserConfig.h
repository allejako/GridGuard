#ifndef USER_CONFIG_H
#define USER_CONFIG_H

typedef struct {
    char   userId[128];
    char   location[64];     // user-friendly display name/city
    double latitude;
    double longitude;
    char   region[16];
    double solarAreaM2;      // m², user's total panel area
    double solarEfficiency;  // 0–1, panel efficiency (e.g. 0.18)
    double consumptionKwh;   // average hourly base load (kWh/h)

    // Grid fees (kr/kWh) - time-of-use tariffs
    double gridFee_low;      // 00:00-06:59 (night rate)
    double gridFee_normal;   // 07:00-16:59 (day rate)
    double gridFee_high;     // 17:00-23:59 (peak rate)

    long   updatedAt;
} UserConfig;

#endif // USER_CONFIG_H
