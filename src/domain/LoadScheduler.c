#include "domain/LoadScheduler.h"
#include "sys/Logger.h"

#include <float.h>

int LoadScheduler_FindWindow(
    const SchedulerEntry *entries, int count,
    int durationMinutes, double powerKw,
    time_t deadline, time_t nowTime,
    ScheduleWindow *out)
{
    if (!entries || count <= 0 || durationMinutes <= 0 || powerKw <= 0.0 || !out)
        return -1;

    // Number of 15-minute quarter slots needed (round up).
    // Example: 90 minutes = 6 quarters, 100 minutes = 7 quarters
    int quartersNeeded = (durationMinutes + 14) / 15;

    if (quartersNeeded < 1 || quartersNeeded > count)
        return -1;

    double bestCost = -1.0;
    int    bestIdx  = -1;
    double nowCost  = -1.0;  // Cost of "start as early as possible" window

    // Sliding window over 15-minute quarters
    for (int i = 0; i <= count - quartersNeeded; i++)
    {
        // Skip slots that are already in the past.
        if (entries[i].timestamp < nowTime)
            continue;

        // The window ends durationMinutes after its start.
        time_t windowEnd = entries[i].timestamp + (time_t)(durationMinutes * 60);
        if (deadline > 0 && windowEnd > deadline)
            continue;

        // Accumulate energy cost for this window across all quarters.
        // Each quarter is 15 minutes = 0.25 hours.
        // NOW: Account for solar production - only pay grid price for net import
        double windowCost = 0.0;
        for (int j = 0; j < quartersNeeded; j++)
        {
            // Calculate fraction of power consumed in this quarter:
            // - Full quarters (except possibly the last): 15 minutes = 0.25 hours
            // - Last quarter might be partial
            int remainingMinutes = durationMinutes - (j * 15);
            double quarterHours = (remainingMinutes >= 15) ? 0.25 : (remainingMinutes / 60.0);

            // Total load energy this quarter (kWh)
            double loadEnergy = powerKw * quarterHours;

            // Solar production available this quarter (kWh per 15min)
            double solarAvailable = entries[i + j].productionKwh;

            // Net grid import: load minus solar production (if available)
            // Example: 2.75 kW load × 0.25h = 0.6875 kWh
            //          Solar produces 0.4 kWh → grid import = 0.2875 kWh
            double gridImport = loadEnergy - solarAvailable;
            if (gridImport < 0.0) gridImport = 0.0;  // Can't use more solar than available

            windowCost += entries[i + j].totalCostPerKwh * gridImport;
        }

        // First valid window = "start now" reference for savings calculation.
        if (nowCost < 0.0)
            nowCost = windowCost;

        if (bestCost < 0.0 || windowCost < bestCost)
        {
            bestCost = windowCost;
            bestIdx  = i;
        }
    }

    if (bestIdx < 0)
    {
        LOG_WARNING("LoadScheduler: No valid window found (duration=%dmin, power=%.1fkW)", durationMinutes, powerKw);
        return -1;
    }

    out->scheduledStart   = entries[bestIdx].timestamp;
    out->durationMinutes  = durationMinutes;
    out->powerKw          = powerKw;
    out->estimatedCostSek = bestCost;
    out->savingsSek       = (nowCost > bestCost) ? (nowCost - bestCost) : 0.0;

    LOG_INFO("LoadScheduler: Best window start=%ld cost=%.2f SEK savings=%.2f SEK", (long)out->scheduledStart, out->estimatedCostSek, out->savingsSek);

    return 0;
}
