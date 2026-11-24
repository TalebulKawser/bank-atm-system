
#include "transaction.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static int is_valid_amount(double amount)
{
    if (!isfinite(amount) || amount <= 0.0)
    {
        return 0;
    }

    return 1;
}

static int prepare_statement(sqlite3 *db,
                             const char *sql,
                             sqlite3_stmt **statement)
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

static int execute_sql(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *statement = NULL;

    if (prepare_statement(db, sql, &statement) != OK)
    {
        return ERR_DB;
    }

    if (sqlite3_step(statement) != SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        return ERR_DB;
    }

    if (sqlite3_finalize(statement) != SQLITE_OK)
    {
        return ERR_DB;
    }

    return OK;
}

static int generate_timestamp(char timestamp[20])
{
    time_t current_time;
    struct tm *local_time;

    if (timestamp == NULL)
    {
        return ERR_INVALID_INPUT;
    }

    current_time = time(NULL);

    if (current_time == (time_t)-1)
    {
        return ERR_DB;
    }

    local_time = localtime(&current_time);

    if (local_time == NULL)
    {
        return ERR_DB;
    }

    if (strftime(timestamp, 20, "%Y-%m-%d %H:%M:%S",
                 local_time) == 0)
    {
        return ERR_DB;
    }

    return OK;
}

static void transaction_delay(void)
{
#ifdef _WIN32
    Sleep(1000);
#else
    sleep(1);
#endif
}

static void rollback_transaction(sqlite3 *bank_db)
{
    execute_sql(bank_db, "ROLLBACK;");
}

static int get_active_account_balance(sqlite3 *bank_db,
                                      sqlite3_int64 account_id,
                                      double *out_balance)
{
    sqlite3_stmt *statement = NULL;
    int result;
    int step_result;

    if (bank_db == NULL || out_balance == NULL || account_id <= 0)
    {
        return ERR_INVALID_INPUT;
    }

    result = prepare_statement(
        bank_db,
        "SELECT balance, status FROM accounts WHERE account_id = ?1;",
        &statement);

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

        if (status == NULL ||
            strcmp((const char *)status, "active") != 0)
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

static int get_savings_balance(sqlite3 *bank_db,
                               sqlite3_int64 account_id,
                               double *out_balance)
{
    sqlite3_stmt *statement = NULL;
    int step_result;

    if (bank_db == NULL || out_balance == NULL || account_id <= 0)
    {
        return ERR_INVALID_INPUT;
    }

    if (prepare_statement(
            bank_db,
            "SELECT balance FROM accounts "
            "WHERE account_id = ?1 "
            "AND status = 'active' "
            "AND account_type = 'savings';",
            &statement) != OK)
    {
        return ERR_DB;
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
        return ERR_WRONG_ACCOUNT_TYPE;
    }

    if (step_result != SQLITE_ROW)
    {
        sqlite3_finalize(statement);
        return ERR_DB;
    }

    *out_balance = sqlite3_column_double(statement, 0);

    if (sqlite3_finalize(statement) != SQLITE_OK)
    {
        return ERR_DB;
    }

    return OK;
}

static int insert_transaction(sqlite3 *bank_db,
                              sqlite3_int64 account_id,
                              const char *type,
                              double amount,
                              sqlite3_int64 related_account_id,
                              int has_related_account)
{
    sqlite3_stmt *statement = NULL;
    char timestamp[20];

    if (bank_db == NULL || account_id <= 0 || type == NULL ||
        !is_valid_amount(amount))
    {
        return ERR_INVALID_INPUT;
    }

    if (generate_timestamp(timestamp) != OK)
    {
        return ERR_DB;
    }

    if (prepare_statement(
            bank_db,
            "INSERT INTO transactions "
            "(account_id, type, amount, related_account_id, status, timestamp) "
            "VALUES (?1, ?2, ?3, ?4, 'completed', ?5);",
            &statement) != OK)
    {
        return ERR_DB;
    }

    if (sqlite3_bind_int64(statement, 1, account_id) != SQLITE_OK ||
        sqlite3_bind_text(statement, 2, type, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_double(statement, 3, amount) != SQLITE_OK)
    {
        sqlite3_finalize(statement);
        return ERR_DB;
    }

    if (has_related_account)
    {
        if (sqlite3_bind_int64(statement, 4, related_account_id) != SQLITE_OK)
        {
            sqlite3_finalize(statement);
            return ERR_DB;
        }
    }
    else
    {
        if (sqlite3_bind_null(statement, 4) != SQLITE_OK)
        {
            sqlite3_finalize(statement);
            return ERR_DB;
        }
    }

    if (sqlite3_bind_text(statement, 5, timestamp, -1,
                          SQLITE_TRANSIENT) != SQLITE_OK)
    {
        sqlite3_finalize(statement);
        return ERR_DB;
    }

    if (sqlite3_step(statement) != SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        return ERR_DB;
    }

    if (sqlite3_finalize(statement) != SQLITE_OK)
    {
        return ERR_DB;
    }

    return OK;
}

int deposit(sqlite3 *bank_db,
            sqlite3_int64 account_id,
            double amount,
            TransactionLinkCheck link_check)
{
    double current_balance;
    sqlite3_stmt *statement = NULL;
    int result;

    if (bank_db == NULL || account_id <= 0 || link_check == NULL)
    {
        return ERR_INVALID_INPUT;
    }

    if (!is_valid_amount(amount))
    {
        return ERR_INVALID_AMOUNT;
    }

    if (!link_check())
    {
        return ERR_LINK_DOWN;
    }

    result = get_active_account_balance(
        bank_db,
        account_id,
        &current_balance);

    if (result != OK)
    {
        return result;
    }

    result = execute_sql(bank_db, "BEGIN TRANSACTION;");

    if (result != OK)
    {
        return result;
    }

    result = prepare_statement(
        bank_db,
        "UPDATE accounts "
        "SET balance = balance + ?1 "
        "WHERE account_id = ?2 AND status = 'active';",
        &statement);

    if (result != OK)
    {
        rollback_transaction(bank_db);
        return result;
    }

    if (sqlite3_bind_double(statement, 1, amount) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 2, account_id) != SQLITE_OK)
    {
        sqlite3_finalize(statement);
        rollback_transaction(bank_db);
        return ERR_DB;
    }

    if (sqlite3_step(statement) != SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        rollback_transaction(bank_db);
        return ERR_DB;
    }

    if (sqlite3_finalize(statement) != SQLITE_OK)
    {
        rollback_transaction(bank_db);
        return ERR_DB;
    }

    if (sqlite3_changes(bank_db) != 1)
    {
        rollback_transaction(bank_db);
        return ERR_ACCOUNT_INACTIVE;
    }

    result = insert_transaction(
        bank_db,
        account_id,
        "deposit",
        amount,
        0,
        0);

    if (result != OK)
    {
        rollback_transaction(bank_db);
        return result;
    }

    transaction_delay();

    if (!link_check())
    {
        rollback_transaction(bank_db);
        return ERR_LINK_DOWN;
    }

    result = execute_sql(bank_db, "COMMIT;");

    if (result != OK)
    {
        rollback_transaction(bank_db);
        return ERR_DB;
    }

    return OK;
}

int withdraw(sqlite3 *bank_db,
             sqlite3_int64 account_id,
             double amount,
             TransactionLinkCheck link_check)
{
    double current_balance;
    sqlite3_stmt *statement = NULL;
    int result;

    if (bank_db == NULL || account_id <= 0 || link_check == NULL)
    {
        return ERR_INVALID_INPUT;
    }

    if (!is_valid_amount(amount))
    {
        return ERR_INVALID_AMOUNT;
    }

    if (!link_check())
    {
        return ERR_LINK_DOWN;
    }

    result = get_active_account_balance(
        bank_db,
        account_id,
        &current_balance);

    if (result != OK)
    {
        return result;
    }

    if (current_balance < amount)
    {
        return ERR_INSUFFICIENT_FUNDS;
    }

    result = execute_sql(bank_db, "BEGIN TRANSACTION;");

    if (result != OK)
    {
        return result;
    }

    result = prepare_statement(
        bank_db,
        "UPDATE accounts "
        "SET balance = balance - ?1 "
        "WHERE account_id = ?2 "
        "AND status = 'active' "
        "AND balance >= ?1;",
        &statement);

    if (result != OK)
    {
        rollback_transaction(bank_db);
        return result;
    }

    if (sqlite3_bind_double(statement, 1, amount) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 2, account_id) != SQLITE_OK)
    {
        sqlite3_finalize(statement);
        rollback_transaction(bank_db);
        return ERR_DB;
    }

    if (sqlite3_step(statement) != SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        rollback_transaction(bank_db);
        return ERR_DB;
    }

    if (sqlite3_finalize(statement) != SQLITE_OK)
    {
        rollback_transaction(bank_db);
        return ERR_DB;
    }

    if (sqlite3_changes(bank_db) != 1)
    {
        rollback_transaction(bank_db);
        return ERR_INSUFFICIENT_FUNDS;
    }

    result = insert_transaction(
        bank_db,
        account_id,
        "withdraw",
        amount,
        0,
        0);

    if (result != OK)
    {
        rollback_transaction(bank_db);
        return result;
    }

    transaction_delay();

    if (!link_check())
    {
        rollback_transaction(bank_db);
        return ERR_LINK_DOWN;
    }

    result = execute_sql(bank_db, "COMMIT;");

    if (result != OK)
    {
        rollback_transaction(bank_db);
        return ERR_DB;
    }

    return OK;
}

int transfer(sqlite3 *bank_db,
             sqlite3_int64 sender_account_id,
             sqlite3_int64 receiver_account_id,
             double amount,
             TransactionLinkCheck link_check)
{
    double sender_balance;
    double receiver_balance;

    sqlite3_stmt *statement = NULL;
    int result;

    if (bank_db == NULL ||
        sender_account_id <= 0 ||
        receiver_account_id <= 0 ||
        link_check == NULL)
    {
        return ERR_INVALID_INPUT;
    }

    if (sender_account_id == receiver_account_id)
    {
        return ERR_INVALID_ACCOUNT;
    }

    if (!is_valid_amount(amount))
    {
        return ERR_INVALID_AMOUNT;
    }

    if (!link_check())
    {
        return ERR_LINK_DOWN;
    }

    result = get_active_account_balance(
        bank_db,
        sender_account_id,
        &sender_balance);

    if (result != OK)
    {
        return result;
    }

    result = get_active_account_balance(
        bank_db,
        receiver_account_id,
        &receiver_balance);

    if (result != OK)
    {
        return result;
    }

    if (sender_balance < amount)
    {
        return ERR_INSUFFICIENT_FUNDS;
    }

    result = execute_sql(bank_db, "BEGIN TRANSACTION;");

    if (result != OK)
    {
        return result;
    }

    result = prepare_statement(
        bank_db,
        "UPDATE accounts "
        "SET balance = balance - ?1 "
        "WHERE account_id = ?2 "
        "AND status = 'active' "
        "AND balance >= ?1;",
        &statement);

    if (result != OK)
    {
        rollback_transaction(bank_db);
        return result;
    }

    if (sqlite3_bind_double(statement, 1, amount) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 2, sender_account_id) != SQLITE_OK)
    {
        sqlite3_finalize(statement);
        rollback_transaction(bank_db);
        return ERR_DB;
    }

    if (sqlite3_step(statement) != SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        rollback_transaction(bank_db);
        return ERR_DB;
    }

    if (sqlite3_finalize(statement) != SQLITE_OK)
    {
        rollback_transaction(bank_db);
        return ERR_DB;
    }

    if (sqlite3_changes(bank_db) != 1)
    {
        rollback_transaction(bank_db);
        return ERR_INSUFFICIENT_FUNDS;
    }

    result = prepare_statement(
        bank_db,
        "UPDATE accounts "
        "SET balance = balance + ?1 "
        "WHERE account_id = ?2 AND status = 'active';",
        &statement);

    if (result != OK)
    {
        rollback_transaction(bank_db);
        return result;
    }

    if (sqlite3_bind_double(statement, 1, amount) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 2, receiver_account_id) != SQLITE_OK)
    {
        sqlite3_finalize(statement);
        rollback_transaction(bank_db);
        return ERR_DB;
    }

    if (sqlite3_step(statement) != SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        rollback_transaction(bank_db);
        return ERR_DB;
    }

    if (sqlite3_finalize(statement) != SQLITE_OK)
    {
        rollback_transaction(bank_db);
        return ERR_DB;
    }

    if (sqlite3_changes(bank_db) != 1)
    {
        rollback_transaction(bank_db);
        return ERR_ACCOUNT_INACTIVE;
    }

    result = insert_transaction(
        bank_db,
        sender_account_id,
        "transfer_out",
        amount,
        receiver_account_id,
        1);

    if (result != OK)
    {
        rollback_transaction(bank_db);
        return result;
    }

    result = insert_transaction(
        bank_db,
        receiver_account_id,
        "transfer_in",
        amount,
        sender_account_id,
        1);

    if (result != OK)
    {
        rollback_transaction(bank_db);
        return result;
    }

    transaction_delay();

    if (!link_check())
    {
        rollback_transaction(bank_db);
        return ERR_LINK_DOWN;
    }

    result = execute_sql(bank_db, "COMMIT;");

    if (result != OK)
    {
        rollback_transaction(bank_db);
        return ERR_DB;
    }

    return OK;
}

int apply_interest(sqlite3 *bank_db,
                   sqlite3_int64 account_id,
                   double interest_rate_percent)
{
    double current_balance;
    double interest_amount;

    sqlite3_stmt *statement = NULL;
    int result;

    if (bank_db == NULL || account_id <= 0)
    {
        return ERR_INVALID_INPUT;
    }

    if (!isfinite(interest_rate_percent) ||
        interest_rate_percent <= 0.0)
    {
        return ERR_INVALID_AMOUNT;
    }

    result = get_savings_balance(
        bank_db,
        account_id,
        &current_balance);

    if (result != OK)
    {
        return result;
    }

    interest_amount = current_balance *
                      (interest_rate_percent / 100.0);

    if (interest_amount <= 0.0)
    {
        return OK;
    }

    result = execute_sql(bank_db, "BEGIN TRANSACTION;");

    if (result != OK)
    {
        return result;
    }

    result = prepare_statement(
        bank_db,
        "UPDATE accounts "
        "SET balance = balance + "
        "(balance * ?1 / 100.0) "
        "WHERE account_id = ?2 "
        "AND status = 'active' "
        "AND account_type = 'savings';",
        &statement);

    if (result != OK)
    {
        rollback_transaction(bank_db);
        return result;
    }

    if (sqlite3_bind_double(statement, 1,
                            interest_rate_percent) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 2, account_id) != SQLITE_OK)
    {
        sqlite3_finalize(statement);
        rollback_transaction(bank_db);
        return ERR_DB;
    }

    if (sqlite3_step(statement) != SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        rollback_transaction(bank_db);
        return ERR_DB;
    }

    if (sqlite3_finalize(statement) != SQLITE_OK)
    {
        rollback_transaction(bank_db);
        return ERR_DB;
    }

    if (sqlite3_changes(bank_db) != 1)
    {
        rollback_transaction(bank_db);
        return ERR_WRONG_ACCOUNT_TYPE;
    }

    result = insert_transaction(
        bank_db,
        account_id,
        "interest",
        interest_amount,
        0,
        0);

    if (result != OK)
    {
        rollback_transaction(bank_db);
        return result;
    }

    result = execute_sql(bank_db, "COMMIT;");

    if (result != OK)
    {
        rollback_transaction(bank_db);
        return ERR_DB;
    }

    return OK;
}

int get_transaction_history(sqlite3 *bank_db,
                            sqlite3_int64 account_id,
                            TransactionRecord records[],
                            size_t capacity,
                            size_t *record_count)
{
    sqlite3_stmt *statement = NULL;
    int step_result;
    size_t count = 0;

    if (record_count == NULL || bank_db == NULL || account_id <= 0)
    {
        return ERR_INVALID_INPUT;
    }

    *record_count = 0;

    if (capacity > 0 && records == NULL)
    {
        return ERR_INVALID_INPUT;
    }

    if (capacity == 0)
    {
        return OK;
    }

    if (prepare_statement(
            bank_db,
            "SELECT txn_id, account_id, type, amount, "
            "related_account_id, timestamp "
            "FROM transactions "
            "WHERE account_id = ?1 AND status = 'completed' "
            "ORDER BY timestamp DESC, txn_id DESC "
            "LIMIT ?2;",
            &statement) != OK)
    {
        return ERR_DB;
    }

    if (sqlite3_bind_int64(statement, 1, account_id) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 2, (sqlite3_int64)capacity) != SQLITE_OK)
    {
        sqlite3_finalize(statement);
        return ERR_DB;
    }

    while ((step_result = sqlite3_step(statement)) == SQLITE_ROW)
    {
        const unsigned char *type;
        const unsigned char *timestamp;

        records[count].txn_id =
            sqlite3_column_int64(statement, 0);

        records[count].account_id =
            sqlite3_column_int64(statement, 1);

        type = sqlite3_column_text(statement, 2);

        if (type == NULL)
        {
            sqlite3_finalize(statement);
            return ERR_DB;
        }

        snprintf(records[count].type,
                 sizeof(records[count].type),
                 "%s",
                 (const char *)type);

        records[count].amount =
            sqlite3_column_double(statement, 3);

        if (sqlite3_column_type(statement, 4) == SQLITE_NULL)
        {
            records[count].related_account_id = 0;
            records[count].has_related_account = 0;
        }
        else
        {
            records[count].related_account_id =
                sqlite3_column_int64(statement, 4);

            records[count].has_related_account = 1;
        }

        timestamp = sqlite3_column_text(statement, 5);

        if (timestamp == NULL)
        {
            sqlite3_finalize(statement);
            return ERR_DB;
        }

        snprintf(records[count].timestamp,
                 sizeof(records[count].timestamp),
                 "%s",
                 (const char *)timestamp);

        count++;
    }

    if (step_result != SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        return ERR_DB;
    }

    if (sqlite3_finalize(statement) != SQLITE_OK)
    {
        return ERR_DB;
    }

    *record_count = count;

    return OK;
}
