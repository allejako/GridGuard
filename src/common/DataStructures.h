#ifndef DATA_STRUCTURES_H
#define DATA_STRUCTURES_H

#include <time.h>
#include <stdbool.h>

// Weather data for solar production forecasting
typedef struct
{
    time_t timestamp;
    double solarIrradiance;    // W/m²
    double cloudCover;         // 0-100%
    double temperature;        // °C
    double windSpeed;          // m/s
    double humidity;           // 0-100%
    bool valid;
} WeatherData;

typedef struct
{
    WeatherData forecasts[96];  // 24h with 15-min intervals
    int count;
    time_t lastUpdated;
} WeatherForecast;

// Electricity spot prices
typedef struct
{
    time_t timestamp;
    double priceSekPerKwh;
    double priceEurPerMwh;
    char region[8];            // SE1, SE2, SE3, SE4
    bool valid;
} SpotPrice;

typedef struct
{
    SpotPrice prices[96];      // 24h spot prices
    int count;
    time_t lastUpdated;
} SpotPriceData;

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
} EnergyPlanEntry;

// Complete energy plan for 24-72 hours
typedef struct
{
    EnergyPlanEntry entries[288];  // 72h with 15-min intervals
    int count;
    time_t generatedAt;
    double totalCostSek;
    double totalGridImportKwh;
    double totalGridExportKwh;
    double totalBatteryCycles;
} EnergyPlan;

// Solar panel configuration
typedef struct
{
    double panelEfficiency;       // 0-1, typically 0.15-0.22
    double panelAreaM2;
    double orientationDegrees;    // 0=north, 90=east, 180=south, 270=west
    double tiltDegrees;           // 0-90°
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
bool WeatherData_IsValid(const WeatherData *data);
bool SpotPrice_IsValid(const SpotPrice *price);
bool EnergyPlanEntry_IsValid(const EnergyPlanEntry *entry);

#endif // DATA_STRUCTURES_H
