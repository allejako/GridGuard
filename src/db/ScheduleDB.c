#include "db/ScheduleDB.h"
#include "sys/Logger.h"
#include <string.h>

static const char *INSERT_SQL =
    "INSERT INTO schedules"
    "  (schedule_id, user_id, load_id, scheduled_start, duration_minutes,"
    "   power_kw, estimated_cost_sek, savings_sek, status, created_at)"
    "  VALUES (?,?,?,?,?,?,?,?,?,?);";

static const char *GET_SQL =
    "SELECT schedule_id, user_id, load_id, scheduled_start, duration_minutes,"
    "       power_kw, estimated_cost_sek, savings_sek, status, created_at"
    "  FROM schedules"
    "  WHERE user_id = ? AND status != 'cancelled'"
    "  ORDER BY scheduled_start ASC;";

static const char *DELETE_SQL =
    "UPDATE schedules SET status = 'cancelled'"
    "  WHERE schedule_id = ? AND user_id = ?;";

int ScheduleDB_Insert(ClientDB *db, const ScheduleEntry *entry)
{
    if (!db || !db->initialized || !entry)
        return -1;

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->db, INSERT_SQL, -1, &stmt, NULL) != SQLITE_OK)
    {
        LOG_ERROR("ScheduleDB_Insert: prepare failed: %s", sqlite3_errmsg(db->db));
        return -1;
    }

    sqlite3_bind_text(stmt,   1, entry->scheduleId,       -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt,   2, entry->userId,            -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt,   3, entry->loadId,            -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt,  4, (sqlite3_int64)entry->scheduledStart);
    sqlite3_bind_int(stmt,    5, entry->durationMinutes);
    sqlite3_bind_double(stmt, 6, entry->powerKw);
    sqlite3_bind_double(stmt, 7, entry->estimatedCostSek);
    sqlite3_bind_double(stmt, 8, entry->savingsSek);
    sqlite3_bind_text(stmt,   9, entry->status,            -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 10, (sqlite3_int64)entry->createdAt);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
    {
        LOG_ERROR("ScheduleDB_Insert: step failed: %s", sqlite3_errmsg(db->db));
        return -1;
    }

    return 0;
}

int ScheduleDB_GetByUser(ClientDB *db, const char *userId,
                         ScheduleEntry *out, int maxCount, int *count)
{
    if (!db || !db->initialized || !userId || !out || maxCount <= 0 || !count)
        return -1;

    *count = 0;

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->db, GET_SQL, -1, &stmt, NULL) != SQLITE_OK)
    {
        LOG_ERROR("ScheduleDB_GetByUser: prepare failed: %s", sqlite3_errmsg(db->db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, userId, -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW && *count < maxCount)
    {
        ScheduleEntry *e = &out[*count];
        memset(e, 0, sizeof(*e));

        const char *col;
        col = (const char *)sqlite3_column_text(stmt, 0);
        if (col) strncpy(e->scheduleId, col, sizeof(e->scheduleId) - 1);
        col = (const char *)sqlite3_column_text(stmt, 1);
        if (col) strncpy(e->userId, col, sizeof(e->userId) - 1);
        col = (const char *)sqlite3_column_text(stmt, 2);
        if (col) strncpy(e->loadId, col, sizeof(e->loadId) - 1);
        e->scheduledStart    = (time_t)sqlite3_column_int64(stmt, 3);
        e->durationMinutes   = sqlite3_column_int(stmt,         4);
        e->powerKw           = sqlite3_column_double(stmt,      5);
        e->estimatedCostSek  = sqlite3_column_double(stmt,      6);
        e->savingsSek        = sqlite3_column_double(stmt,      7);
        col = (const char *)sqlite3_column_text(stmt, 8);
        if (col) strncpy(e->status, col, sizeof(e->status) - 1);
        e->createdAt         = (time_t)sqlite3_column_int64(stmt, 9);

        (*count)++;
    }

    sqlite3_finalize(stmt);
    return 0;
}

int ScheduleDB_Delete(ClientDB *db, const char *scheduleId, const char *userId)
{
    if (!db || !db->initialized || !scheduleId || !userId)
        return -1;

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->db, DELETE_SQL, -1, &stmt, NULL) != SQLITE_OK)
    {
        LOG_ERROR("ScheduleDB_Delete: prepare failed: %s", sqlite3_errmsg(db->db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, scheduleId, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, userId,     -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
    {
        LOG_ERROR("ScheduleDB_Delete: step failed: %s", sqlite3_errmsg(db->db));
        return -1;
    }

    int changed = sqlite3_changes(db->db);
    return (changed > 0) ? 0 : 1;  // 1 = not found / not owned by user
}
