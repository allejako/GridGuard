#ifndef ENERGY_DATA_H
#define ENERGY_DATA_H

#include <time.h>
#include <stdbool.h>

typedef enum
{
    ACTION_BUY_FROM_GRID,   // Low price, no/little solar → buy cheap energy
    ACTION_SELL_TO_GRID,    // Solar surplus → sell to grid
    ACTION_IDLE             // Price normal, no surplus
} EnergyAction;

typedef struct
{
    time_t       timestamp;
    EnergyAction action;
    double       productionKwh;   // Estimated solar production for this hour
    double       consumptionKwh;  // User's base load for this hour
    double       spotPrice;       // SEK/kWh
    bool         valid;
} EnergyDataEntry;

typedef struct
{
    EnergyDataEntry entries[96]; // up to 96h hourly forecast
    int    count;
    time_t generatedAt;
    double totalCostSek;
    double totalGridImportKwh;
    double totalGridExportKwh;
} EnergyData;

const char *EnergyAction_ToString(EnergyAction action);

#endif // ENERGY_DATA_H
