#include "domain/Energy.h"

const char *EnergyAction_ToString(EnergyAction action)
{
    switch (action)
    {
        case ACTION_BUY_FROM_GRID:  return "BUY";
        case ACTION_SELL_TO_GRID:   return "SELL";
        case ACTION_IDLE:           return "IDLE";
        default:                    return "IDLE";
    }
}
