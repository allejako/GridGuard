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

    // Number of hourly slots needed (round up for partial last hour).
    int    fullHours   = durationMinutes / 60;
    double partialFrac = (durationMinutes % 60) / 60.0;
    int    slotsNeeded = fullHours + (partialFrac > 0.0 ? 1 : 0);

    if (slotsNeeded < 1 || slotsNeeded > count)
        return -1;

    double bestCost = -1.0;
    int    bestIdx  = -1;
    double nowCost  = -1.0;  // Cost of "start as early as possible" window

    for (int i = 0; i <= count - slotsNeeded; i++)
    {
        // Skip slots that are already in the past.
        if (entries[i].timestamp < nowTime)
            continue;

        // The window ends durationMinutes after its start.
        time_t windowEnd = entries[i].timestamp + (time_t)(durationMinutes * 60);
        if (deadline > 0 && windowEnd > deadline)
            continue;

        // Accumulate energy cost for this window.
        double windowCost = 0.0;
        for (int j = 0; j < fullHours; j++)
            windowCost += entries[i + j].totalCostPerKwh * powerKw * 1.0; // 1 full hour
        if (partialFrac > 0.0)
            windowCost += entries[i + slotsNeeded - 1].totalCostPerKwh * powerKw * partialFrac;

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
