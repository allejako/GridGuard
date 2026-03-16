#ifndef ENERGY_DATA_H
#define ENERGY_DATA_H

#include <time.h>
#include <stdbool.h>

typedef enum
{
    ACTION_BUY_FROM_GRID,      // Low price — run flexible loads now (EV, laundry, dishwasher)
    ACTION_SELL_TO_GRID,       // Solar surplus and positive spot price — export to grid
    ACTION_AVOID_HIGH_PRICE,   // High price — defer flexible loads if possible
    ACTION_IDLE                // Neither condition met — consume baseline only
} EnergyAction;

typedef struct
{
    time_t       timestamp;
    EnergyAction action;
    double       productionKwh;        // Solar production (kWh per quarter-hour)
    double       consumptionKwh;       // Household load (kWh per quarter-hour)
    double       spotPrice;            // SEK/kWh spot (constant within the hour)
    double       totalCostSek;         // SEK/kWh total (spot + grid + tax + VAT)
    double       priceVsAvgPct;        // % vs median
    bool         valid;
} EnergyDataEntry;

// The best contiguous block of BUY-signal hours in the forecast.
// savingsSek is the total SEK saved vs buying at the median hour,
// assuming actual estimated consumption during each hour of the window.
typedef struct
{
    time_t  start;           // First BUY hour
    time_t  end;             // Last BUY hour
    int     hours;           // Window length
    double  avgCostSek;      // SEK/kWh average consumer cost over the window
    double  savingsSek;      // Total SEK saved vs buying at the median hour
} BuyWindow;

typedef struct
{
    EnergyDataEntry entries[384]; // 96h × 4 quarters/hour (15min resolution)
    int       count;
    time_t    generatedAt;
    double    totalCostSek;
    double    totalGridImportKwh;
    double    totalGridExportKwh;
    BuyWindow bestBuyWindow;
    int       hasBuyWindow;
} EnergyData;

static inline const char *EnergyAction_ToString(EnergyAction action)
{
    switch (action)
    {
        case ACTION_BUY_FROM_GRID:      return "BUY";
        case ACTION_SELL_TO_GRID:       return "SELL";
        case ACTION_AVOID_HIGH_PRICE:   return "AVOID";
        case ACTION_IDLE:               return "IDLE";
        default:                        return "IDLE";
    }
}

#endif // ENERGY_DATA_H
