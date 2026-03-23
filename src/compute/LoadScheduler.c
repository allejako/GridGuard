#include "compute/LoadScheduler.h"
#include "sys/Logger.h"

#include <float.h>
#include <time.h>

// Returns a practicality multiplier based on the local hour of the window start.
// Higher multiplier = more practical (user likely home and able to oversee the load).
//
//   Evening  17–22 → 1.5×  (best: user typically home, can monitor)
//   Night    22–07 → 1.0×  (acceptable: timer-based, no oversight needed)
//   Daytime  07–17 → 0.5×  (worst: user often away)
//
// Only used for window *selection* — estimatedCostSek and savingsSek always
// reflect the unweighted actual energy cost so the user sees real numbers.
static double PracticalityFactor(time_t t)
{
    struct tm local;
    localtime_r(&t, &local);
    int h = local.tm_hour;
    if (h >= 17 && h < 22) return 1.5;
    if (h >= 7  && h < 17) return 0.5;
    return 1.0; /* 22–07 night */
}

int LoadScheduler_FindWindow(const SchedulerEntry *entries, int count, int durationMinutes, double powerKw, time_t deadline, time_t nowTime, ScheduleWindow *out)
{
    if (!entries || count <= 0 || durationMinutes <= 0 || powerKw <= 0.0 || !out)
        return -1;

    // Number of 15-minute quarter slots needed (round up).
    // Example: 90 minutes = 6 quarters, 100 minutes = 7 quarters
    int quartersNeeded = (durationMinutes + 14) / 15;

    if (quartersNeeded < 1 || quartersNeeded > count)
        return -1;

    double bestWeighted = -1.0;  /* weighted cost used for selection only */
    double bestCost     = -1.0;  /* actual (unweighted) cost of chosen window */
    int    bestIdx      = -1;
    double nowCost      = -1.0;  /* actual cost of "start immediately" reference */

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

        // Accumulate actual energy cost for this window across all quarters.
        // Each quarter is 15 minutes = 0.25 hours.
        // Solar production offsets grid import: only pay for net import.
        double windowCost = 0.0;
        for (int j = 0; j < quartersNeeded; j++)
        {
            // Calculate fraction of power consumed in this quarter:
            // full quarters use 0.25 h; the last quarter may be partial.
            int    remainingMinutes = durationMinutes - (j * 15);
            double quarterHours    = (remainingMinutes >= 15) ? 0.25 : (remainingMinutes / 60.0);

            // Total load energy this quarter (kWh)
            double loadEnergy     = powerKw * quarterHours;

            // Solar production available this quarter (kWh per 15 min)
            double solarAvailable = entries[i + j].productionKwh;

            // Net grid import: load minus solar (never negative)
            double gridImport = loadEnergy - solarAvailable;
            if (gridImport < 0.0) gridImport = 0.0;

            windowCost += entries[i + j].totalCostPerKwh * gridImport;
        }

        // First valid window = "start now" reference for savings calculation.
        if (nowCost < 0.0)
            nowCost = windowCost;

        // Divide actual cost by the practicality factor so that a more convenient
        // time slot can beat a cheaper-but-inconvenient one. A higher factor means
        // the window is more practical, which lowers its weighted score.
        double weighted = windowCost / PracticalityFactor(entries[i].timestamp);

        if (bestWeighted < 0.0 || weighted < bestWeighted)
        {
            bestWeighted = weighted;
            bestCost     = windowCost;
            bestIdx      = i;
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
