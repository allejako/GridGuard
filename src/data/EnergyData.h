#ifndef ENERGY_DATA_H
#define ENERGY_DATA_H

#include <time.h>
#include <stdbool.h>

// Solar production calculation
typedef struct
{
    time_t timestamp;
    double productionKwh;
    double efficiencyFactor;   // 0-1
    bool valid;
} SolarProduction;

// Energy plan actions
typedef enum
{
    ACTION_BUY_FROM_GRID,
    ACTION_SELL_TO_GRID,
    ACTION_CHARGE_BATTERY,
    ACTION_DISCHARGE_BATTERY,
    ACTION_DIRECT_USE,
    ACTION_IDLE
} EnergyAction;

// Energy plan entry for 15-minute interval
typedef struct
{
    time_t timestamp;
    EnergyAction action;
    double productionKwh;
    double consumptionKwh;
    double gridPowerKwh;
    double batteryPowerKwh;
    double spotPrice;
    double estimatedCostSek;
    double batterySocPercent;
    bool valid;
} EnergyDataEntry;

// Complete energy plan for 24-72 hours
typedef struct
{
    EnergyDataEntry entries[288];  // 72h with 15-min intervals
    int count;
    time_t generatedAt;
    double totalCostSek;
    double totalGridImportKwh;
    double totalGridExportKwh;
    double totalBatteryCycles;
} EnergyData;

// Solar panel configuration
typedef struct
{
    double panelEfficiency;       // 0-1, typically 0.15-0.22
    double panelAreaM2;
    double orientationDegrees;    // 0=north, 90=east, 180=south, 270=west
    double tiltDegrees;           // 0-90
    double peakPowerKw;
} SolarConfig;

// Battery configuration
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

// Household consumption profile
typedef struct
{
    double baseLoadKw;
    double peakLoadKw;
    double averageDailyKwh;
} ConsumptionProfile;

// Utility functions
const char* EnergyAction_ToString(EnergyAction action);
bool EnergyDataEntry_IsValid(const EnergyDataEntry *entry);

#endif // ENERGY_DATA_H
