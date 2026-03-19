#ifndef _SCHEDULE_DB_H_
#define _SCHEDULE_DB_H_

#include "db/ClientDB.h"
#include <time.h>

#define SCHEDULE_MAX_PER_USER 64

typedef struct
{
    char   scheduleId[160];  // userId (up to 127) + "_" + timestamp (up to 20) + NUL
    char   userId[64];
    char   loadId[64];
    time_t scheduledStart;
    int    durationMinutes;
    double powerKw;
    double estimatedCostSek;
    double savingsSek;
    char   status[16];   // "pending" | "active" | "completed" | "cancelled"
    time_t createdAt;
} ScheduleEntry;

// Insert a new schedule. Returns 0 on success, -1 on failure.
int ScheduleDB_Insert(ClientDB *db, const ScheduleEntry *entry);

// Fetch all schedules for a user (status != 'cancelled').
// out must point to an array of at least maxCount ScheduleEntry elements.
// *count is set to the number of entries written.
// Returns 0 on success, -1 on failure.
int ScheduleDB_GetByUser(ClientDB *db, const char *userId, ScheduleEntry *out, int maxCount, int *count);

// Delete (cancel) a schedule by id, but only if it belongs to userId.
// Returns 0 on success, 1 if not found, -1 on DB error.
int ScheduleDB_Delete(ClientDB *db, const char *scheduleId, const char *userId);

#endif // _SCHEDULE_DB_H_
