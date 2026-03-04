#ifndef PLATFORM_DB_H
#define PLATFORM_DB_H

#include <sqlite3.h>

// Minimal platform database - just user accounts and subscription plans.
// Used to demonstrate that JWT auth comes from a separate platform DB.
// NEVER stores sensitive energy data — that lives client-side only.
typedef struct {
    sqlite3 *handle;
    char path[512];
} PlatformDB;

// Initialize DB and create schema if needed.
// Returns  0 = ok,  -1 = error.
int PlatformDB_Initiate(PlatformDB *db, const char *path);

// Close connection and cleanup.
void PlatformDB_Shutdown(PlatformDB *db);

// Get user info for JWT generation.
// Returns  0 = found,  1 = not found,  -1 = error.
int PlatformDB_GetUser(PlatformDB *db, const char *userId, char *emailOut, char *planOut);

#endif // PLATFORM_DB_H
