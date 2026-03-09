#include "auth/PlatformDB.h"
#include "sys/Logger.h"
#include <string.h>

// Minimal platform DB schema - just users and their subscription tier.
// Used ONLY for demo to show JWT comes from platform, not for production.
static const char *SCHEMA_SQL =
    "CREATE TABLE IF NOT EXISTS users ("
    "    user_id TEXT PRIMARY KEY,"
    "    email TEXT UNIQUE NOT NULL,"
    "    plan_type TEXT NOT NULL,"  // ex. free | pro | enterprise
    "    created_at INTEGER NOT NULL"
    ");";

int PlatformDB_Initiate(PlatformDB *db, const char *path)
{
    if (!db || !path)
        return -1;

    memset(db, 0, sizeof(PlatformDB));
    strncpy(db->path, path, sizeof(db->path) - 1);

    if (sqlite3_open(path, &db->handle) != SQLITE_OK)
    {
        LOG_ERROR("PlatformDB: Failed to open %s: %s", path, sqlite3_errmsg(db->handle));
        return -1;
    }

    char *err_msg = NULL;
    if (sqlite3_exec(db->handle, SCHEMA_SQL, NULL, NULL, &err_msg) != SQLITE_OK)
    {
        LOG_ERROR("PlatformDB: Failed to create schema: %s", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db->handle);
        return -1;
    }

    LOG_INFO("PlatformDB: Initialized at %s", path);
    return 0;
}

void PlatformDB_Shutdown(PlatformDB *db)
{
    if (!db || !db->handle)
        return;

    sqlite3_close(db->handle);
    db->handle = NULL;
    LOG_INFO("PlatformDB: Closed");
}

// Get user by ID for JWT generation.
// Returns 0=found, 1=not found, -1=error.
int PlatformDB_GetUser(PlatformDB *db, const char *userId, char *emailOut, char *planOut)
{
    if (!db || !db->handle || !userId || !emailOut || !planOut)
        return -1;

    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT email, plan_type FROM users WHERE user_id = ?";

    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        LOG_ERROR("PlatformDB: prepare failed: %s", sqlite3_errmsg(db->handle));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, userId, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
    {
        const char *email = (const char *)sqlite3_column_text(stmt, 0);
        const char *plan = (const char *)sqlite3_column_text(stmt, 1);

        strncpy(emailOut, email ? email : "", 255);
        emailOut[255] = '\0';

        strncpy(planOut, plan ? plan : "free", 31);
        planOut[31] = '\0';

        sqlite3_finalize(stmt);
        return 0;  // Found
    }
    else if (rc == SQLITE_DONE)
    {
        sqlite3_finalize(stmt);
        return 1;  // Not found
    }

    LOG_ERROR("PlatformDB: step failed: %s", sqlite3_errmsg(db->handle));
    sqlite3_finalize(stmt);
    return -1;
}
