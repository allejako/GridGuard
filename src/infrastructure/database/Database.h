#ifndef DATABASE_H
#define DATABASE_H

#include <sqlite3.h>
#include <stdbool.h>

typedef struct {
    sqlite3 *db;
    bool     initialized;
} Database;

// Open (or create) the SQLite database at path and create the user_configs table.
// Returns 0 on success, -1 on failure.
int  Database_Initiate(Database *db, const char *path);

// Close the database connection.
void Database_Shutdown(Database *db);

#endif // DATABASE_H
