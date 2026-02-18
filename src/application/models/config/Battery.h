#ifndef _BATTERY_CONFIG_H_
#define _BATTERY_CONFIG_H_

typedef struct
{
    double capacityKwh;
    double maxChargeRateKw;
    double maxDischargeRateKw;
    double minSocPercent;
    double maxSocPercent;
    double currentSocPercent;
    double efficiency;            // 0-1
} BatteryConfig;

#endif // _BATTERY_CONFIG_H_
