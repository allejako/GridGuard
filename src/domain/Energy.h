#ifndef ENERGY_DATA_H
#define ENERGY_DATA_H

#include <time.h>
#include <stdbool.h>

typedef enum
{
    ACTION_BUY_FROM_GRID,   // Low price — run flexible loads now (EV, laundry, dishwasher)
    ACTION_SELL_TO_GRID,    // Solar surplus and positive spot price — export to grid
    ACTION_IDLE             // Neither condition met — consume baseline only
} EnergyAction;

typedef struct
{
    time_t       timestamp;
    EnergyAction action;
    double       productionKwh;        // Estimated solar production this hour
    double       consumptionKwh;       // Estimated household load this hour (time-of-day profile)
    double       spotPrice;            // SEK/kWh — spot price only
    double       totalCostSek;         // SEK/kWh — spot + grid fee + energy tax + VAT
    double       priceVsAvgPct;        // % deviation from forecast average (negative = cheaper than avg)
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
    EnergyDataEntry entries[96]; // Up to 96h hourly forecast
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
        case ACTION_BUY_FROM_GRID:  return "BUY";
        case ACTION_SELL_TO_GRID:   return "SELL";
        case ACTION_IDLE:           return "IDLE";
        default:                    return "IDLE";
    }
}

#endif // ENERGY_DATA_H
