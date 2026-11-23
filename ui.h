#ifndef UI_H
#define UI_H

#include "db.h"
#include "status.h"

/* main.c owns database lifetime and passes open connections to the UI. */
int ui_run_atm(sqlite3 *bank_db, sqlite3 *atm_db);
int ui_run_bank(sqlite3 *bank_db, sqlite3 *atm_db);

#endif
