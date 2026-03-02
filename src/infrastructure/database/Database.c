#include "Database.h"
#include "Logger.h"
#include <string.h>

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
    "  grid_fee_low     REAL NOT NULL DEFAULT 0.25,"
    "  grid_fee_normal  REAL NOT NULL DEFAULT 0.35,"
    "  grid_fee_high    REAL NOT NULL DEFAULT 0.45,"
    "  updated_at       INTEGER NOT NULL"
    ");";

int Database_Initiate(Database *db, const char *path)
{
    if (!db || !path)
        return -1;

    memset(db, 0, sizeof(*db));

    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    if (sqlite3_open_v2(path, &db->db, flags, NULL) != SQLITE_OK)
    {
        LOG_ERROR("Database: Failed to open '%s': %s", path, sqlite3_errmsg(db->db));
        sqlite3_close(db->db);
        db->db = NULL;
        return -1;
    }

    char *errMsg = NULL;
    if (sqlite3_exec(db->db, CREATE_TABLE_SQL, NULL, NULL, &errMsg) != SQLITE_OK)
    {
        LOG_ERROR("Database: Failed to create table: %s", errMsg);
        sqlite3_free(errMsg);
        sqlite3_close(db->db);
        db->db = NULL;
        return -1;
    }

    // Add columns that may be missing from older database files.
    // ALTER TABLE ADD COLUMN returns an error if the column already exists —
    // that is expected and safe to ignore.
    static const char *MIGRATE_SQL[] = {
        "ALTER TABLE user_configs ADD COLUMN grid_fee_low    REAL NOT NULL DEFAULT 0.25;",
        "ALTER TABLE user_configs ADD COLUMN grid_fee_normal REAL NOT NULL DEFAULT 0.35;",
        "ALTER TABLE user_configs ADD COLUMN grid_fee_high   REAL NOT NULL DEFAULT 0.45;",
        NULL
    };
    for (int i = 0; MIGRATE_SQL[i]; i++)
    {
        char *migErr = NULL;
        if (sqlite3_exec(db->db, MIGRATE_SQL[i], NULL, NULL, &migErr) != SQLITE_OK)
        {
            // "duplicate column name" means the column already exists — not an error.
            if (strstr(migErr, "duplicate column name") == NULL)
                LOG_WARNING("Database: Migration warning: %s", migErr);
            sqlite3_free(migErr);
        }
    }

    db->initialized = true;
    LOG_INFO("Database: Opened '%s'", path);
    return 0;
}

void Database_Shutdown(Database *db)
{
    if (!db || !db->initialized)
        return;

    sqlite3_close(db->db);
    db->db = NULL;
    db->initialized = false;
    LOG_INFO("Database: Closed");
}
