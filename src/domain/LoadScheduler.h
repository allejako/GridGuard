#ifndef LOAD_SCHEDULER_H
#define LOAD_SCHEDULER_H

#include <time.h>

// One price slot (15-minute quarter) fed into the scheduler.
typedef struct
{
    time_t timestamp;        // UTC epoch, start of the quarter-hour (15-min interval)
    double totalCostPerKwh;  // all-in: spot + grid fee + energy tax + VAT (SEK/kWh)
    double productionKwh;    // solar production in this quarter (kWh per 15min), 0.0 if no solar
} SchedulerEntry;

// Result returned when a window is found.
typedef struct
{
    time_t scheduledStart;    // UTC epoch — when to start the load
    int    durationMinutes;
    double powerKw;
    double estimatedCostSek;  // total cost for the full run (SEK)
    double savingsSek;        // vs starting immediately (0 if no savings)
} ScheduleWindow;

// Find the cheapest contiguous block in entries[] that fits durationMinutes
// of powerKw consumption and finishes before deadline (pass 0 for no limit).
// Slots before nowTime are skipped. Operates on 15-minute quarters for precise
// scheduling. Returns 0 and fills *out on success, -1 if no valid window exists.
int LoadScheduler_FindWindow(const SchedulerEntry *entries, int count, int durationMinutes, double powerKw, time_t deadline, time_t nowTime, ScheduleWindow *out);

#endif // LOAD_SCHEDULER_H
