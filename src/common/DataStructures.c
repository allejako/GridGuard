#include "DataStructures.h"
#include <string.h>
#include <math.h>

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

bool WeatherData_IsValid(const WeatherData *data)
{
    if (!data || !data->valid)
        return false;

    // Solar irradiance 0-1500 W/m² is reasonable
    if (data->solarIrradiance < 0 || data->solarIrradiance > 1500)
        return false;

    // Cloud cover 0-100%
    if (data->cloudCover < 0 || data->cloudCover > 100)
        return false;

    // Temperature -50 to +50°C is reasonable for Sweden
    if (data->temperature < -50 || data->temperature > 50)
        return false;

    // Timestamp not more than 7 days in the future
    time_t now = time(NULL);
    time_t maxFuture = now + (7 * 24 * 3600);
    if (data->timestamp > maxFuture || data->timestamp < now - 3600)
        return false;

    return true;
}

bool SpotPrice_IsValid(const SpotPrice *price)
{
    if (!price || !price->valid)
        return false;

    // Price 0-10 SEK/kWh is reasonable, negative prices can occur
    if (price->priceSekPerKwh < -1.0 || price->priceSekPerKwh > 10.0)
        return false;

    // Validate region (SE1, SE2, SE3, SE4)
    if (strncmp(price->region, "SE", 2) != 0)
        return false;

    // Validate timestamp
    time_t now = time(NULL);
    time_t maxFuture = now + (7 * 24 * 3600);
    if (price->timestamp > maxFuture || price->timestamp < now - 3600)
        return false;

    return true;
}

bool EnergyPlanEntry_IsValid(const EnergyPlanEntry *entry)
{
    if (!entry || !entry->valid)
        return false;

    // Production 0-100 kWh for 15-min is reasonable for private household
    if (entry->productionKwh < 0 || entry->productionKwh > 100)
        return false;

    // Validate consumption
    if (entry->consumptionKwh < 0 || entry->consumptionKwh > 100)
        return false;

    // Validate battery SoC 0-100%
    if (entry->batterySocPercent < 0 || entry->batterySocPercent > 100)
        return false;

    // Validate timestamp
    time_t now = time(NULL);
    time_t maxFuture = now + (7 * 24 * 3600);
    if (entry->timestamp > maxFuture || entry->timestamp < now - 3600)
        return false;

    return true;
}
