#include <stdio.h>
#include "sqlite3.h"

static int db_execute(sqlite3 *db, const char *sql) // Helper function to execute any sql statements
{
    sqlite3_stmt *stmt = NULL;

    int result = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    if (result != SQLITE_OK)
    {
        fprintf(stderr, "SQL preparation failed: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    result = sqlite3_step(stmt);

    if (result != SQLITE_DONE)
    {
        fprintf(stderr, "SQL execution failed: %s\n", sqlite3_errmsg(db));

        sqlite3_finalize(stmt);
        return -1;
    }

    result = sqlite3_finalize(stmt);

    if (result != SQLITE_OK)
    {
        fprintf(stderr, "SQL statement finalization failed: %s\n",
                sqlite3_errmsg(db));
        return -1;
    }

    return 0;
}

int db_open(sqlite3 **bank_db, sqlite3 **atm_db)
{
    sqlite3 *bank = NULL;
    sqlite3 *atm = NULL;

    if (bank_db == NULL || atm_db == NULL || *bank_db == NULL || *atm_db == NULL)
    {
        fprintf(stderr, "Invalid database pointer argument.\n");
        return -1;
    }

    int result = sqlite3_open("bank.db", &bank);

    if (result != SQLITE_OK)
    {
        fprintf(stderr, "Could not open bank.db: %s\n", sqlite3_errmsg(bank));

        if (bank != NULL)
        {
            sqlite3_close(bank);
        }
        return -1;
    }

    result = sqlite3_open("atm.db", &atm);

    if (result != SQLITE_OK)
    {
        fprintf(stderr, "Could not open atm.db: %s\n", sqlite3_errmsg(atm));

        sqlite3_close(atm);
        if (atm != NULL)
        {
            sqlite3_close(atm);
        }
        return -1;
    }

    if (db_execute(bank, "PRAGMA foreign_keys = ON;") != 0 || db_execute(atm, "PRAGMA foreign_keys = ON;") != 0)
    {
        sqlite3_close(bank);
        sqlite3_close(atm);
        return -1;
    }

    *bank_db = bank;
    *atm_db = atm;

    return 0;
}

int db_init(sqlite3 *bank_db, sqlite3 *atm_db)
{
    if (bank_db == NULL || atm_db == NULL)
    {
        fprintf(stderr, "Cannot initialize NULL database connections.\n");
        return -1;
    }

    if (db_execute(bank_db, "BEGIN TRANSACTION;") != 0)
    {
        return -1;
    }

    if (db_execute(bank_db,

                   "CREATE TABLE IF NOT EXISTS users ("
                   "user_id INTEGER PRIMARY KEY,"
                   "name TEXT NOT NULL,"
                   "pin TEXT NOT NULL,"
                   "status TEXT NOT NULL DEFAULT 'active' "
                   "CHECK(status IN ('active', 'frozen', 'closed')),"
                   "created_at TEXT NOT NULL"
                   ");") != 0 ||
        db_execute(bank_db,
                   "CREATE TABLE IF NOT EXISTS accounts ("
                   "account_id INTEGER PRIMARY KEY,"
                   "user_id INTEGER NOT NULL,"
                   "account_type TEXT NOT NULL "
                   "CHECK(account_type IN ('savings', 'checking')),"
                   "balance REAL NOT NULL DEFAULT 0.0 "
                   "CHECK(balance >= 0),"
                   "status TEXT NOT NULL DEFAULT 'active' "
                   "CHECK(status IN ('active', 'frozen', 'closed')),"
                   "FOREIGN KEY(user_id) REFERENCES users(user_id)"
                   ");") != 0 ||
        db_execute(bank_db,
                   "CREATE TABLE IF NOT EXISTS transactions ("
                   "txn_id INTEGER PRIMARY KEY,"
                   "account_id INTEGER NOT NULL,"
                   "type TEXT NOT NULL "
                   "CHECK(type IN ('deposit', 'withdraw', "
                   "'transfer_out', 'transfer_in', 'interest')),"
                   "amount REAL NOT NULL CHECK(amount > 0),"
                   "related_account_id INTEGER,"
                   "status TEXT NOT NULL DEFAULT 'completed' "
                   "CHECK(status IN ('completed', 'failed')),"
                   "timestamp TEXT NOT NULL,"
                   "FOREIGN KEY(account_id) REFERENCES accounts(account_id),"
                   "FOREIGN KEY(related_account_id) REFERENCES accounts(account_id)"
                   ");") != 0)
    {
        db_execute(bank_db, "ROLLBACK;");
        return -1;
    }

    if (db_execute(bank_db, "COMMIT;") != 0)
    {
        db_execute(bank_db, "ROLLBACK;");
        return -1;
    }

    if (db_execute(atm_db, "BEGIN TRANSACTION;") != 0)
    {
        return -1;
    }

    if (db_execute(atm_db,
                   "CREATE TABLE IF NOT EXISTS pending_queue ("
                   "queue_id INTEGER PRIMARY KEY,"
                   "account_id INTEGER NOT NULL,"
                   "type TEXT NOT NULL "
                   "CHECK(type IN ('deposit', 'transfer')),"
                   "amount REAL NOT NULL CHECK(amount > 0),"
                   "related_account_id INTEGER,"
                   "queued_at TEXT NOT NULL,"
                   "sync_status TEXT NOT NULL DEFAULT 'pending' "
                   "CHECK(sync_status IN ('pending', 'synced', 'failed'))"
                   ");") != 0)
    {
        db_execute(atm_db, "ROLLBACK;");
        return -1;
    }

    if (db_execute(atm_db, "COMMIT;") != 0)
    {
        db_execute(atm_db, "ROLLBACK;");
        return -1;
    }

    return 0;
}

int db_close(sqlite3 **bank_db, sqlite3 **atm_db)
{
    int status = 0;

    if (bank_db == NULL || atm_db == NULL)
    {
        fprintf(stderr, "Invalid database pointer arguments.\n");
        return -1;
    }

    if (*bank_db != NULL)
    {
        int result = sqlite3_close(*bank_db);

        if (result == SQLITE_OK)
        {
            *bank_db = NULL;
        }
        else
        {
            fprintf(stderr, "Could not close bank database: %s\n",
                    sqlite3_errstr(result));
            status = -1;
        }
    }

    if (*atm_db != NULL)
    {
        int result = sqlite3_close(*atm_db);

        if (result == SQLITE_OK)
        {
            *atm_db = NULL;
        }
        else
        {
            fprintf(stderr, "Could not close ATM database: %s\n",
                    sqlite3_errstr(result));
            status = -1;
        }
    }

    return status;
}