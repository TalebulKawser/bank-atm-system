
#include <stdio.h>
#include "db.h"

/*
 * Checks whether a table exists in a database.
 * Returns 0 if found, -1 otherwise.
 */
static int verify_table(sqlite3 *db, const char *table_name)
{
    sqlite3_stmt *stmt = NULL;

    const char *sql =
        "SELECT COUNT(*) FROM sqlite_master "
        "WHERE type = 'table' AND name = ?;";

    int result = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    if (result != SQLITE_OK)
    {
        fprintf(stderr, "Could not prepare table verification: %s\n",
                sqlite3_errmsg(db));
        return -1;
    }

    /* Bind the table name to the question-mark placeholder. */
    result = sqlite3_bind_text(stmt, 1, table_name, -1, SQLITE_TRANSIENT);

    if (result != SQLITE_OK)
    {
        fprintf(stderr, "Could not bind table name: %s\n",
                sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }

    /* Execute the query and retrieve its result. */
    result = sqlite3_step(stmt);

    if (result != SQLITE_ROW)
    {
        fprintf(stderr, "Could not retrieve table result: %s\n",
                sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }

    int count = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);

    if (count == 1)
    {
        printf("[PASS] Table '%s' exists.\n", table_name);
        return 0;
    }

    printf("[FAIL] Table '%s' does not exist.\n", table_name);
    return -1;
}

/*
 * Tests the unique account-type constraint.
 * Returns 0 if all checks pass, -1 otherwise.
 */
static int test_account_uniqueness(sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;
    int result;
    int status = -1;
    sqlite3_int64 first_user_id;
    sqlite3_int64 second_user_id;

    /* Keep test data temporary. */
    if (sqlite3_exec(db, "SAVEPOINT uniqueness_test;", NULL, NULL, NULL) != SQLITE_OK)
    {
        fprintf(stderr, "Could not create test savepoint: %s\n",
                sqlite3_errmsg(db));
        return -1;
    }

    /* Create the first test user. */
    const char *user_sql =
        "INSERT INTO users (name, pin, status, created_at) "
        "VALUES (?, ?, 'active', ?);";

    result = sqlite3_prepare_v2(db, user_sql, -1, &stmt, NULL);

    if (result != SQLITE_OK)
        goto cleanup;

    sqlite3_bind_text(stmt, 1, "Uniqueness Test User 1", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, "1234", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, "2026-11-18 00:00:00", -1, SQLITE_STATIC);

    result = sqlite3_step(stmt);

    if (result != SQLITE_DONE)
        goto cleanup;

    sqlite3_finalize(stmt);
    stmt = NULL;

    first_user_id = sqlite3_last_insert_rowid(db);

    /* Create the first user's savings account. */
    const char *account_sql =
        "INSERT INTO accounts (user_id, account_type) VALUES (?, ?);";

    result = sqlite3_prepare_v2(db, account_sql, -1, &stmt, NULL);

    if (result != SQLITE_OK)
        goto cleanup;

    sqlite3_bind_int64(stmt, 1, first_user_id);
    sqlite3_bind_text(stmt, 2, "savings", -1, SQLITE_STATIC);

    result = sqlite3_step(stmt);

    if (result != SQLITE_DONE)
        goto cleanup;

    sqlite3_finalize(stmt);
    stmt = NULL;

    printf("[PASS] First savings account was created.\n");

    /* Attempt a second savings account for the same user. */
    result = sqlite3_prepare_v2(db, account_sql, -1, &stmt, NULL);

    if (result != SQLITE_OK)
        goto cleanup;

    sqlite3_bind_int64(stmt, 1, first_user_id);
    sqlite3_bind_text(stmt, 2, "savings", -1, SQLITE_STATIC);

    result = sqlite3_step(stmt);

    if (result != SQLITE_CONSTRAINT)
        goto cleanup;

    sqlite3_finalize(stmt);
    stmt = NULL;

    printf("[PASS] Duplicate savings account was rejected.\n");

    /* The same user should still be able to open checking. */
    result = sqlite3_prepare_v2(db, account_sql, -1, &stmt, NULL);

    if (result != SQLITE_OK)
        goto cleanup;

    sqlite3_bind_int64(stmt, 1, first_user_id);
    sqlite3_bind_text(stmt, 2, "checking", -1, SQLITE_STATIC);

    result = sqlite3_step(stmt);

    if (result != SQLITE_DONE)
        goto cleanup;

    sqlite3_finalize(stmt);
    stmt = NULL;

    printf("[PASS] First user's checking account was created.\n");

    /* Create a second user. */
    result = sqlite3_prepare_v2(db, user_sql, -1, &stmt, NULL);

    if (result != SQLITE_OK)
        goto cleanup;

    sqlite3_bind_text(stmt, 1, "Uniqueness Test User 2", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, "5678", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, "2026-11-18 00:00:00", -1, SQLITE_STATIC);

    result = sqlite3_step(stmt);

    if (result != SQLITE_DONE)
        goto cleanup;

    sqlite3_finalize(stmt);
    stmt = NULL;

    second_user_id = sqlite3_last_insert_rowid(db);

    /* A different user should be allowed to create savings. */
    result = sqlite3_prepare_v2(db, account_sql, -1, &stmt, NULL);

    if (result != SQLITE_OK)
        goto cleanup;

    sqlite3_bind_int64(stmt, 1, second_user_id);
    sqlite3_bind_text(stmt, 2, "savings", -1, SQLITE_STATIC);

    result = sqlite3_step(stmt);

    if (result != SQLITE_DONE)
        goto cleanup;

    printf("[PASS] Different user can create a savings account.\n");

    status = 0;

cleanup:
    if (stmt != NULL)
        sqlite3_finalize(stmt);

    /* Undo all test inserts. */
    sqlite3_exec(db, "ROLLBACK TO uniqueness_test;", NULL, NULL, NULL);
    sqlite3_exec(db, "RELEASE uniqueness_test;", NULL, NULL, NULL);

    if (status != 0)
    {
        fprintf(stderr, "[FAIL] Account uniqueness test failed: %s\n",
                sqlite3_errmsg(db));
    }

    return status;
}

int main(void)
{
    sqlite3 *bank_db = NULL;
    sqlite3 *atm_db = NULL;

    printf("=== DATABASE MODULE TEST ===\n\n");

    /* Test 1: Open both databases. */
    if (db_open(&bank_db, &atm_db) != 0)
    {
        fprintf(stderr, "[FAIL] Could not open databases.\n");
        return 1;
    }

    printf("[PASS] Both databases opened.\n\n");

    /* Test 2: Initialize the database tables. */
    if (db_init(bank_db, atm_db) != 0)
    {
        fprintf(stderr, "[FAIL] Database initialization failed.\n");
        db_close(&bank_db, &atm_db);
        return 1;
    }

    printf("[PASS] Database initialization completed.\n\n");

    /* Test 3: Verify the bank database tables. */
    printf("--- Bank database ---\n");

    if (verify_table(bank_db, "users") != 0 ||
        verify_table(bank_db, "accounts") != 0 ||
        verify_table(bank_db, "transactions") != 0)
    {
        db_close(&bank_db, &atm_db);
        return 1;
    }

    /* Test 4: Verify the ATM database table. */
    printf("\n--- ATM database ---\n");

    if (verify_table(atm_db, "pending_queue") != 0)
    {
        db_close(&bank_db, &atm_db);
        return 1;
    }
    printf("\n--- Account uniqueness test ---\n");

    if (test_account_uniqueness(bank_db) != 0)
    {
        db_close(&bank_db, &atm_db);
        return 1;
    }

    /* Test 5: Initialize again to check repeatability. */
    printf("\n--- Reinitialization test ---\n");

    if (db_init(bank_db, atm_db) != 0)
    {
        fprintf(stderr, "[FAIL] Reinitialization failed.\n");
        db_close(&bank_db, &atm_db);
        return 1;
    }

    printf("[PASS] Reinitialization completed without errors.\n");

    /* Test 6: Close both databases. */
    printf("\n--- Closing databases ---\n");

    if (db_close(&bank_db, &atm_db) != 0)
    {
        fprintf(stderr, "[FAIL] Could not close databases cleanly.\n");
        return 1;
    }

    if (bank_db != NULL || atm_db != NULL)
    {
        fprintf(stderr, "[FAIL] Database pointers were not reset.\n");
        return 1;
    }

    printf("[PASS] Both databases closed.\n");
    printf("[PASS] Both connection pointers are NULL.\n");

    printf("\n=== ALL DATABASE TESTS PASSED ===\n");

    return 0;
}
