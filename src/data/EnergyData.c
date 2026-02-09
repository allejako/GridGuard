#include "EnergyData.h"
#include <string.h>

const char* EnergyAction_ToString(EnergyAction action)
{
    switch (action)
    {
        case ACTION_BUY_FROM_GRID:      return "BUY_FROM_GRID";
        case ACTION_SELL_TO_GRID:       return "SELL_TO_GRID";
        case ACTION_CHARGE_BATTERY:     return "CHARGE_BATTERY";
        case ACTION_DISCHARGE_BATTERY:  return "DISCHARGE_BATTERY";
        case ACTION_DIRECT_USE:         return "DIRECT_USE";
        case ACTION_IDLE:               return "IDLE";
        default:                        return "UNKNOWN";
    }
}

bool EnergyDataEntry_IsValid(const EnergyDataEntry *entry)
{
    if (!entry || !entry->valid)
        return false;

    if (entry->productionKwh < 0 || entry->productionKwh > 100)
        return false;

    if (entry->consumptionKwh < 0 || entry->consumptionKwh > 100)
        return false;

    if (entry->batterySocPercent < 0 || entry->batterySocPercent > 100)
        return false;

    time_t now = time(NULL);
    time_t maxFuture = now + (7 * 24 * 3600);
    if (entry->timestamp > maxFuture || entry->timestamp < now - 3600)
        return false;

    return true;
}
