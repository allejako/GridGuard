#ifndef CLIENT_DB_H
#define CLIENT_DB_H

#include <sqlite3.h>
#include <stdbool.h>

// Client database: user config, schedules, energy data.
// Stored ONLY on user's device — never sent to platform server.
// Ensures privacy: all sensitive energy consumption data stays local.
typedef struct {
    sqlite3 *db;
    bool     initialized;
} ClientDB;

// Initialize client DB and create schema if needed.
// Returns  0 = ok,  -1 = error.
int ClientDB_Initiate(ClientDB *db, const char *path);

// Close connection and cleanup.
void ClientDB_Shutdown(ClientDB *db);

#endif // CLIENT_DB_H
