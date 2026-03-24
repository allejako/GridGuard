#include "db/ClientDB.h"
#include "sys/Logger.h"
#include <string.h>

static const char *CREATE_SCHEDULES_SQL =
    "CREATE TABLE IF NOT EXISTS schedules ("
    "  schedule_id        TEXT PRIMARY KEY,"
    "  user_id            TEXT NOT NULL,"
    "  load_id            TEXT NOT NULL,"
    "  scheduled_start    INTEGER NOT NULL,"
    "  duration_minutes   INTEGER NOT NULL,"
    "  power_kw           REAL NOT NULL,"
    "  estimated_cost_sek REAL NOT NULL,"
    "  savings_sek        REAL NOT NULL DEFAULT 0.0,"
    "  status             TEXT NOT NULL DEFAULT 'pending',"
    "  created_at         INTEGER NOT NULL"
    ");";

static const char *CREATE_TABLE_SQL =
    "CREATE TABLE IF NOT EXISTS user_configs ("
    "  user_id          TEXT PRIMARY KEY,"
    "  location         TEXT,"
    "  latitude         REAL NOT NULL,"
    "  longitude        REAL NOT NULL,"
    "  region           TEXT NOT NULL,"
    "  solar_area_m2    REAL NOT NULL,"
    "  solar_efficiency REAL NOT NULL,"
    "  consumption_kwh  REAL NOT NULL DEFAULT 0.5,"
    "  grid_fee_low      REAL NOT NULL DEFAULT 0.25,"
    "  grid_fee_normal   REAL NOT NULL DEFAULT 0.35,"
    "  grid_fee_high     REAL NOT NULL DEFAULT 0.45,"
    "  panel_tilt_deg    REAL NOT NULL DEFAULT 30.0,"
    "  panel_azimuth_deg REAL NOT NULL DEFAULT 180.0,"
    "  updated_at        INTEGER NOT NULL"
    ");";

int ClientDB_Initiate(ClientDB *db, const char *path)
{
    if (!db || !path)
        return -1;

    memset(db, 0, sizeof(*db));

    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    if (sqlite3_open_v2(path, &db->db, flags, NULL) != SQLITE_OK)
    {
        LOG_ERROR("ClientDB: Failed to open '%s': %s", path, sqlite3_errmsg(db->db));
        sqlite3_close(db->db);
        db->db = NULL;
        return -1;
    }

    char *errMsg = NULL;
    if (sqlite3_exec(db->db, CREATE_TABLE_SQL, NULL, NULL, &errMsg) != SQLITE_OK)
    {
        LOG_ERROR("ClientDB: Failed to create table: %s", errMsg);
        sqlite3_free(errMsg);
        sqlite3_close(db->db);
        db->db = NULL;
        return -1;
    }

    if (sqlite3_exec(db->db, CREATE_SCHEDULES_SQL, NULL, NULL, &errMsg) != SQLITE_OK)
    {
        LOG_ERROR("ClientDB: Failed to create schedules table: %s", errMsg);
        sqlite3_free(errMsg);
        sqlite3_close(db->db);
        db->db = NULL;
        return -1;
    }

    // Add columns that may be missing from older database files.
    // ALTER TABLE ADD COLUMN returns an error if the column already exists —
    // that is expected and safe to ignore.
    static const char *MIGRATE_SQL[] = {
        "ALTER TABLE user_configs ADD COLUMN grid_fee_low      REAL NOT NULL DEFAULT 0.25;",
        "ALTER TABLE user_configs ADD COLUMN grid_fee_normal   REAL NOT NULL DEFAULT 0.35;",
        "ALTER TABLE user_configs ADD COLUMN grid_fee_high     REAL NOT NULL DEFAULT 0.45;",
        "ALTER TABLE user_configs ADD COLUMN panel_tilt_deg    REAL NOT NULL DEFAULT 30.0;",
        "ALTER TABLE user_configs ADD COLUMN panel_azimuth_deg REAL NOT NULL DEFAULT 180.0;",
        NULL
    };
    for (int i = 0; MIGRATE_SQL[i]; i++)
    {
        char *migErr = NULL;
        if (sqlite3_exec(db->db, MIGRATE_SQL[i], NULL, NULL, &migErr) != SQLITE_OK)
        {
            // "duplicate column name" means the column already exists — not an error.
            if (strstr(migErr, "duplicate column name") == NULL)
                LOG_WARNING("ClientDB: Migration warning: %s", migErr);
            sqlite3_free(migErr);
        }
    }

    db->initialized = true;
    LOG_INFO("ClientDB: Opened '%s'", path);
    return 0;
}

void ClientDB_Shutdown(ClientDB *db)
{
    if (!db || !db->initialized)
        return;

    sqlite3_close(db->db);
    db->db = NULL;
    db->initialized = false;
    LOG_INFO("ClientDB: Closed");
}
