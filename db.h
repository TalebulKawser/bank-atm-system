#ifndef DB_H
#define DB_H

#include "sqlite3.h"

int db_open(sqlite3 **bank_db, sqlite3 **atm_db);

int db_init(sqlite3 *bank_db, sqlite3 *atm_db);

int db_close(sqlite3 **bank_db, sqlite3 **atm_db);

#endif