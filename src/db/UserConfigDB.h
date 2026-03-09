#ifndef USER_CONFIG_DB_H
#define USER_CONFIG_DB_H

#include "db/ClientDB.h"
#include "domain/UserConfig.h"

// Fetch config for userId into *out.
// Returns  0 = found,  1 = not found,  -1 = error.
int UserConfigDB_Get(ClientDB *db, const char *userId, UserConfig *out);

// Insert or replace config (sets updated_at = time(NULL)).
// Returns  0 = ok,  -1 = error.
int UserConfigDB_Upsert(ClientDB *db, const UserConfig *config);

#endif // USER_CONFIG_DB_H
