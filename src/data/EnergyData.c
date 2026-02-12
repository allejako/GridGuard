// MOCK IMPLEMENTATION - Not yet fully implemented
// This file contains minimal utility functions for EnergyData

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
    // Simple validation - just check if entry exists and is marked valid
    return (entry != NULL && entry->valid);
}
