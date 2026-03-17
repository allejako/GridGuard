#include "db/UserConfigDB.h"
#include "sys/Logger.h"
#include <string.h>
#include <time.h>

static const char *GET_SQL =
    "SELECT location, latitude, longitude, region, solar_area_m2, solar_efficiency, consumption_kwh, grid_fee_low, grid_fee_normal, grid_fee_high, updated_at"
    "  FROM user_configs WHERE user_id = ?;";

static const char *UPSERT_SQL =
    "INSERT OR REPLACE INTO user_configs"
    "  (user_id, location, latitude, longitude, region, solar_area_m2, solar_efficiency, consumption_kwh, grid_fee_low, grid_fee_normal, grid_fee_high, updated_at)"
    "  VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

int UserConfigDB_Get(ClientDB *db, const char *userId, UserConfig *out)
{
    if (!db || !db->initialized || !userId || !out)
        return -1;

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->db, GET_SQL, -1, &stmt, NULL) != SQLITE_OK)
    {
        LOG_ERROR("UserConfigDB_Get: prepare failed: %s", sqlite3_errmsg(db->db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, userId, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
        memset(out, 0, sizeof(*out));
        strncpy(out->userId, userId, sizeof(out->userId) - 1);
        const char *location = (const char *)sqlite3_column_text(stmt, 0);
        if (location)
            strncpy(out->location, location, sizeof(out->location) - 1);
        out->latitude        = sqlite3_column_double(stmt, 1);
        out->longitude       = sqlite3_column_double(stmt, 2);
        const char *region   = (const char *)sqlite3_column_text(stmt, 3);
        if (region)
            strncpy(out->region, region, sizeof(out->region) - 1);
        out->solarAreaM2     = sqlite3_column_double(stmt, 4);
        out->solarEfficiency = sqlite3_column_double(stmt, 5);
        out->consumptionKwh  = sqlite3_column_double(stmt, 6);
        out->gridFee_low     = sqlite3_column_double(stmt, 7);
        out->gridFee_normal  = sqlite3_column_double(stmt, 8);
        out->gridFee_high    = sqlite3_column_double(stmt, 9);
        out->updatedAt       = (long)sqlite3_column_int64(stmt, 10);
        sqlite3_finalize(stmt);
        return 0;
    }
    else if (rc == SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        return 1; // not found
    }

    LOG_ERROR("UserConfigDB_Get: step failed: %s", sqlite3_errmsg(db->db));
    sqlite3_finalize(stmt);
    return -1;
}

int UserConfigDB_Upsert(ClientDB *db, const UserConfig *config)
{
    if (!db || !db->initialized || !config)
        return -1;

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->db, UPSERT_SQL, -1, &stmt, NULL) != SQLITE_OK)
    {
        LOG_ERROR("UserConfigDB_Upsert: prepare failed: %s", sqlite3_errmsg(db->db));
        return -1;
    }

    sqlite3_bind_text(stmt,   1, config->userId, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt,   2, config->location, -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 3, config->latitude);
    sqlite3_bind_double(stmt, 4, config->longitude);
    sqlite3_bind_text(stmt,   5, config->region, -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 6, config->solarAreaM2);
    sqlite3_bind_double(stmt, 7, config->solarEfficiency);
    sqlite3_bind_double(stmt, 8, config->consumptionKwh);
    sqlite3_bind_double(stmt, 9, config->gridFee_low);
    sqlite3_bind_double(stmt, 10, config->gridFee_normal);
    sqlite3_bind_double(stmt, 11, config->gridFee_high);
    sqlite3_bind_int64(stmt,  12, (sqlite3_int64)time(NULL));

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
    {
        LOG_ERROR("UserConfigDB_Upsert: step failed: %s", sqlite3_errmsg(db->db));
        return -1;
    }

    return 0;
}
