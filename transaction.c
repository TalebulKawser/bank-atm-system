#include <string.h>
#include <math.h>

#include "transaction.h"

static int is_valid_amount(double amount)
{
    if (!isfinite(amount) || amount <= 0.0)
    {
        return 0;
    }
    return 1;
}

static int prepare_statement(sqlite3 *db, const char *sql, sqlite3_stmt **statement)
{
    if (db == NULL || sql == NULL || statement == NULL)
    {
        return ERR_INVALID_INPUT;
    }

    *statement = NULL;

    if (sqlite3_prepare_v2(db, sql, -1, statement, NULL) != SQLITE_OK)
    {
        return ERR_DB;
    }

    return OK;
}

static int get_active_account_balance(sqlite3 *bank_db, sqlite3_int64 account_id, double *out_balance)
{
    sqlite3_stmt *statement = NULL;
    int result;
    int step_result;

    if (bank_db == NULL || out_balance == NULL || account_id <= 0)
    {
        return ERR_INVALID_INPUT;
    }

    result = prepare_statement(bank_db, "SELECT balance, status FROM accounts WHERE account_id = ?1;", &statement);

    if (result != OK)
    {
        return result;
    }

    if (sqlite3_bind_int64(statement, 1, account_id) != SQLITE_OK)
    {
        sqlite3_finalize(statement);
        return ERR_DB;
    }

    step_result = sqlite3_step(statement);

    if (step_result == SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        return ERR_NOT_FOUND;
    }

    if (step_result != SQLITE_ROW)
    {
        sqlite3_finalize(statement);
        return ERR_DB;
    }

    {
        const unsigned char *status = sqlite3_column_text(statement, 1);
        if (status == NULL || strcmp((const char *)status, "active") != 0)
        {
            sqlite3_finalize(statement);
            return ERR_ACCOUNT_INACTIVE;
        }
    }

    *out_balance = sqlite3_column_double(statement, 0);

    if (sqlite3_finalize(statement) != SQLITE_OK)
    {
        return ERR_DB;
    }

    return OK;
}