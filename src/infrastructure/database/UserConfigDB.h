#ifndef USER_CONFIG_DB_H
#define USER_CONFIG_DB_H

#include "Database.h"
#include "UserConfig.h"

// Fetch config for userId into *out.
// Returns  0 = found,  1 = not found,  -1 = error.
int UserConfigDB_Get(Database *db, const char *userId, UserConfig *out);

// Insert or replace config (sets updated_at = time(NULL)).
// Returns  0 = ok,  -1 = error.
int UserConfigDB_Upsert(Database *db, const UserConfig *config);

#endif // USER_CONFIG_DB_H
