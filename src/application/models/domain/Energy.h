#ifndef ENERGY_DATA_H
#define ENERGY_DATA_H

#include <time.h>
#include <stdbool.h>
#include "Solar.h"
#include "Battery.h"
#include "Consumption.h"

typedef struct
{
    time_t timestamp;
    double productionKwh;
    double efficiencyFactor;   // 0-1
    bool valid;
} SolarProduction;

typedef enum
{
    ACTION_BUY_FROM_GRID,
    ACTION_SELL_TO_GRID,
    ACTION_CHARGE_BATTERY,
    ACTION_DISCHARGE_BATTERY,
    ACTION_DIRECT_USE,
    ACTION_IDLE
} EnergyAction;

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

const char* EnergyAction_ToString(EnergyAction action);
bool EnergyDataEntry_IsValid(const EnergyDataEntry *entry);

#endif // ENERGY_DATA_H
