#ifndef LOAD_SCHEDULER_H
#define LOAD_SCHEDULER_H

#include <time.h>

// One hourly slot used by the scheduler.
typedef struct
{
    time_t timestamp;        // Start of this hour (UTC epoch)
    double totalCostPerKwh;  // SEK/kWh — spot + grid fee + energy tax + VAT
} SchedulerEntry;

// Result of FindWindow.
typedef struct
{
    time_t scheduledStart;    // Optimal start time (UTC epoch)
    int    durationMinutes;   // As requested
    double powerKw;           // As requested
    double estimatedCostSek;  // Total energy cost for the scheduled window (SEK)
    double savingsSek;        // Compared to starting immediately (SEK, >= 0)
} ScheduleWindow;

// Find the cheapest contiguous time window for a shiftable load.
//
// entries       - Hourly cost data sorted ascending by timestamp.
// count         - Number of entries (typically 96).
// durationMinutes - How long the load needs to run (e.g. 216 for 3h36m).
// powerKw       - Load power in kW (e.g. 11.0 for an EV charger).
// deadline      - Unix timestamp by which the load must finish (0 = no deadline).
// nowTime       - Current time; windows starting before this are skipped.
// out           - Populated with the best window on success.
//
// Returns 0 on success, -1 if no valid window exists within the constraints.
int LoadScheduler_FindWindow(
    const SchedulerEntry *entries, int count,
    int durationMinutes, double powerKw,
    time_t deadline, time_t nowTime,
    ScheduleWindow *out);

#endif // LOAD_SCHEDULER_H
