
#include "db.h"
#include "transaction.h"
#include "status.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------
   Test state
   --------------------------------------------------------- */

static int tests_passed = 0;
static int tests_failed = 0;

static int simulated_link_up = 1;
static int callback_calls = 0;
static int drop_on_second_check = 0;


/* ---------------------------------------------------------
   Gateway simulation
   --------------------------------------------------------- */

/*
 * This function imitates the gateway callback that the
 * queue module will eventually provide through is_link_up().
 *
 * Normally, it returns the current simulated link status.
 *
 * For one special test, it returns:
 *   First call  -> link is up
 *   Second call -> link is down
 */
static int test_link_check(void)
{
    callback_calls++;

    if (drop_on_second_check && callback_calls >= 2)
    {
        return 0;
    }

    return simulated_link_up;
}


/* ---------------------------------------------------------
   Test reporting
   --------------------------------------------------------- */

static void report_test(const char *description, int condition)
{
    if (condition)
    {
        printf("[PASS] %s\n", description);
        tests_passed++;
    }
    else
    {
        printf("[FAIL] %s\n", description);
        tests_failed++;
    }
}


/* ---------------------------------------------------------
   Database fixture helpers
   --------------------------------------------------------- */

/*
 * Creates a temporary user and returns the generated user ID.
 */
static int create_test_user(sqlite3 *db,
                            const char *name,
                            sqlite3_int64 *out_user_id)
{
    sqlite3_stmt *statement = NULL;

    const char *sql =
        "INSERT INTO users (name, pin, status, created_at) "
        "VALUES (?1, '1234', 'active', '2026-11-24 10:00:00');";

    if (db == NULL || name == NULL || out_user_id == NULL)
    {
        return 0;
    }

    if (sqlite3_prepare_v2(db, sql, -1, &statement, NULL) != SQLITE_OK)
    {
        return 0;
    }

    if (sqlite3_bind_text(statement, 1, name, -1,
                          SQLITE_TRANSIENT) != SQLITE_OK)
    {
        sqlite3_finalize(statement);
        return 0;
    }

    if (sqlite3_step(statement) != SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        return 0;
    }

    if (sqlite3_finalize(statement) != SQLITE_OK)
    {
        return 0;
    }

    *out_user_id = sqlite3_last_insert_rowid(db);

    return 1;
}


/*
 * Creates a temporary account and returns its generated ID.
 */
static int create_test_account(sqlite3 *db,
                               sqlite3_int64 user_id,
                               const char *account_type,
                               double initial_balance,
                               sqlite3_int64 *out_account_id)
{
    sqlite3_stmt *statement = NULL;

    const char *sql =
        "INSERT INTO accounts "
        "(user_id, account_type, balance, status) "
        "VALUES (?1, ?2, ?3, 'active');";

    if (db == NULL || user_id <= 0 ||
        account_type == NULL || out_account_id == NULL)
    {
        return 0;
    }

    if (sqlite3_prepare_v2(db, sql, -1, &statement, NULL) != SQLITE_OK)
    {
        return 0;
    }

    if (sqlite3_bind_int64(statement, 1, user_id) != SQLITE_OK ||
        sqlite3_bind_text(statement, 2, account_type, -1,
                          SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_double(statement, 3, initial_balance) != SQLITE_OK)
    {
        sqlite3_finalize(statement);
        return 0;
    }

    if (sqlite3_step(statement) != SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        return 0;
    }

    if (sqlite3_finalize(statement) != SQLITE_OK)
    {
        return 0;
    }

    *out_account_id = sqlite3_last_insert_rowid(db);

    return 1;
}


/*
 * Reads an account balance directly from the database.
 * This is used by the test program to verify results.
 */
static int read_balance(sqlite3 *db,
                        sqlite3_int64 account_id,
                        double *out_balance)
{
    sqlite3_stmt *statement = NULL;

    const char *sql =
        "SELECT balance FROM accounts WHERE account_id = ?1;";

    if (db == NULL || account_id <= 0 || out_balance == NULL)
    {
        return 0;
    }

    if (sqlite3_prepare_v2(db, sql, -1, &statement, NULL) != SQLITE_OK)
    {
        return 0;
    }

    if (sqlite3_bind_int64(statement, 1, account_id) != SQLITE_OK)
    {
        sqlite3_finalize(statement);
        return 0;
    }

    if (sqlite3_step(statement) != SQLITE_ROW)
    {
        sqlite3_finalize(statement);
        return 0;
    }

    *out_balance = sqlite3_column_double(statement, 0);

    if (sqlite3_finalize(statement) != SQLITE_OK)
    {
        return 0;
    }

    return 1;
}


/*
 * Removes the temporary test data.
 *
 * Transactions are deleted first, followed by accounts,
 * followed by users.
 */
static int cleanup_test_data(sqlite3 *db,
                             sqlite3_int64 user_id_1,
                             sqlite3_int64 user_id_2,
                             sqlite3_int64 account_id_1,
                             sqlite3_int64 account_id_2,
                             sqlite3_int64 account_id_3)
{
    sqlite3_stmt *statement = NULL;

    const char *delete_transactions =
        "DELETE FROM transactions "
        "WHERE account_id = ?1 OR account_id = ?2 OR account_id = ?3;";

    const char *delete_accounts =
        "DELETE FROM accounts WHERE user_id = ?1 OR user_id = ?2;";

    const char *delete_users =
        "DELETE FROM users WHERE user_id = ?1 OR user_id = ?2;";

    if (sqlite3_prepare_v2(db, delete_transactions, -1,
                           &statement, NULL) != SQLITE_OK)
    {
        return 0;
    }

    if (sqlite3_bind_int64(statement, 1, account_id_1) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 2, account_id_2) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 3, account_id_3) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        return 0;
    }

    if (sqlite3_finalize(statement) != SQLITE_OK)
    {
        return 0;
    }

    statement = NULL;

    if (sqlite3_prepare_v2(db, delete_accounts, -1,
                           &statement, NULL) != SQLITE_OK)
    {
        return 0;
    }

    if (sqlite3_bind_int64(statement, 1, user_id_1) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 2, user_id_2) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        return 0;
    }

    if (sqlite3_finalize(statement) != SQLITE_OK)
    {
        return 0;
    }

    statement = NULL;

    if (sqlite3_prepare_v2(db, delete_users, -1,
                           &statement, NULL) != SQLITE_OK)
    {
        return 0;
    }

    if (sqlite3_bind_int64(statement, 1, user_id_1) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 2, user_id_2) != SQLITE_OK ||
        sqlite3_step(statement) != SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        return 0;
    }

    if (sqlite3_finalize(statement) != SQLITE_OK)
    {
        return 0;
    }

    return 1;
}


/* ---------------------------------------------------------
   Main test program
   --------------------------------------------------------- */

int main(void)
{
    sqlite3 *bank_db = NULL;
    sqlite3 *atm_db = NULL;

    sqlite3_int64 user_id_1 = 0;
    sqlite3_int64 user_id_2 = 0;

    sqlite3_int64 sender_id = 0;
    sqlite3_int64 receiver_id = 0;
    sqlite3_int64 savings_id = 0;

    double balance = 0.0;

    TransactionRecord records[10];
    size_t record_count = 0;

    int result;
    int setup_success = 1;
    int cleanup_success;

    printf("\n====================================\n");
    printf("       TRANSACTION MODULE TEST\n");
    printf("====================================\n\n");


    /* -----------------------------------------------------
       1. Open and initialize databases
       ----------------------------------------------------- */

    result = db_open(&bank_db, &atm_db);

    if (result != OK)
    {
        printf("[FAIL] Could not open databases.\n");
        return 1;
    }

    report_test("Both databases opened.", 1);

    result = db_init(bank_db, atm_db);

    if (result != OK)
    {
        printf("[FAIL] Could not initialize databases.\n");
        db_close(&bank_db, &atm_db);
        return 1;
    }

    report_test("Database initialization completed.", 1);


    /* -----------------------------------------------------
       2. Create temporary users and accounts
       ----------------------------------------------------- */

    setup_success =
        create_test_user(bank_db, "Transaction Test User 1",
                         &user_id_1) &&
        create_test_user(bank_db, "Transaction Test User 2",
                         &user_id_2) &&
        create_test_account(bank_db, user_id_1, "checking",
                            1000.00, &sender_id) &&
        create_test_account(bank_db, user_id_2, "checking",
                            200.00, &receiver_id) &&
        create_test_account(bank_db, user_id_1, "savings",
                            1000.00, &savings_id);

    report_test("Temporary users and accounts created.", setup_success);

    if (!setup_success)
    {
        printf("\nTest setup failed. Inspect bank.db before continuing.\n");
        db_close(&bank_db, &atm_db);
        return 1;
    }


    /* -----------------------------------------------------
       3. Deposit test
       ----------------------------------------------------- */

    simulated_link_up = 1;
    callback_calls = 0;
    drop_on_second_check = 0;

    result = deposit(bank_db, sender_id, 500.00, test_link_check);

    report_test("Deposit of 500 returns OK.", result == OK);

    if (read_balance(bank_db, sender_id, &balance))
    {
        report_test("Deposit increases balance to 1500.",
                    fabs(balance - 1500.00) < 0.000001);
    }
    else
    {
        report_test("Could read balance after deposit.", 0);
    }


    /* -----------------------------------------------------
       4. Withdrawal test
       ----------------------------------------------------- */

    callback_calls = 0;

    result = withdraw(bank_db, sender_id, 200.00, test_link_check);

    report_test("Withdrawal of 200 returns OK.", result == OK);

    if (read_balance(bank_db, sender_id, &balance))
    {
        report_test("Withdrawal reduces balance to 1300.",
                    fabs(balance - 1300.00) < 0.000001);
    }
    else
    {
        report_test("Could read balance after withdrawal.", 0);
    }


    /* -----------------------------------------------------
       5. Insufficient funds test
       ----------------------------------------------------- */

    callback_calls = 0;

    result = withdraw(bank_db, sender_id, 5000.00, test_link_check);

    report_test("Withdrawal exceeding balance is rejected.",
                result == ERR_INSUFFICIENT_FUNDS);

    if (read_balance(bank_db, sender_id, &balance))
    {
        report_test("Rejected withdrawal leaves balance unchanged.",
                    fabs(balance - 1300.00) < 0.000001);
    }
    else
    {
        report_test("Could read balance after rejected withdrawal.", 0);
    }


    /* -----------------------------------------------------
       6. Successful transfer test
       ----------------------------------------------------- */

    callback_calls = 0;

    result = transfer(bank_db, sender_id, receiver_id,
                      100.00, test_link_check);

    report_test("Transfer of 100 returns OK.", result == OK);

    if (read_balance(bank_db, sender_id, &balance))
    {
        report_test("Sender balance becomes 1200.",
                    fabs(balance - 1200.00) < 0.000001);
    }
    else
    {
        report_test("Could read sender balance after transfer.", 0);
    }

    if (read_balance(bank_db, receiver_id, &balance))
    {
        report_test("Receiver balance becomes 300.",
                    fabs(balance - 300.00) < 0.000001);
    }
    else
    {
        report_test("Could read receiver balance after transfer.", 0);
    }


    /* -----------------------------------------------------
       7. Transfer to the same account
       ----------------------------------------------------- */

    callback_calls = 0;

    result = transfer(bank_db, sender_id, sender_id,
                      100.00, test_link_check);

    report_test("Transfer to the same account is rejected.",
                result == ERR_INVALID_ACCOUNT);


    /* -----------------------------------------------------
       8. Transfer with insufficient funds
       ----------------------------------------------------- */

    callback_calls = 0;

    result = transfer(bank_db, sender_id, receiver_id,
                      5000.00, test_link_check);

    report_test("Transfer exceeding balance is rejected.",
                result == ERR_INSUFFICIENT_FUNDS);

    if (read_balance(bank_db, sender_id, &balance))
    {
        report_test("Failed transfer leaves sender balance unchanged.",
                    fabs(balance - 1200.00) < 0.000001);
    }
    else
    {
        report_test("Could read sender balance after failed transfer.", 0);
    }

    if (read_balance(bank_db, receiver_id, &balance))
    {
        report_test("Failed transfer leaves receiver balance unchanged.",
                    fabs(balance - 300.00) < 0.000001);
    }
    else
    {
        report_test("Could read receiver balance after failed transfer.", 0);
    }


    /* -----------------------------------------------------
       9. Interest test
       ----------------------------------------------------- */

    result = apply_interest(bank_db, savings_id, 5.0);

    report_test("Applying 5 percent savings interest returns OK.",
                result == OK);

    if (read_balance(bank_db, savings_id, &balance))
    {
        report_test("Savings balance becomes 1050.",
                    fabs(balance - 1050.00) < 0.000001);
    }
    else
    {
        report_test("Could read savings balance after interest.", 0);
    }


    /* -----------------------------------------------------
       10. Interest on checking account
       ----------------------------------------------------- */

    result = apply_interest(bank_db, sender_id, 5.0);

    report_test("Interest on checking account is rejected.",
                result == ERR_WRONG_ACCOUNT_TYPE);


    /* -----------------------------------------------------
       11. Transaction history test
       ----------------------------------------------------- */

    result = get_transaction_history(
        bank_db,
        sender_id,
        records,
        10,
        &record_count
    );

    report_test("Transaction history retrieval returns OK.",
                result == OK);

    report_test("Sender has exactly three completed transactions.",
                result == OK && record_count == 3);

    if (result == OK && record_count == 3)
    {
        report_test("Newest sender transaction is transfer_out.",
                    strcmp(records[0].type, "transfer_out") == 0);

        report_test("Second sender transaction is withdraw.",
                    strcmp(records[1].type, "withdraw") == 0);

        report_test("Oldest sender transaction is deposit.",
                    strcmp(records[2].type, "deposit") == 0);
    }


    /* -----------------------------------------------------
       12. Gateway initially offline
       ----------------------------------------------------- */

    simulated_link_up = 0;
    callback_calls = 0;
    drop_on_second_check = 0;

    result = deposit(bank_db, sender_id, 50.00, test_link_check);

    report_test("Offline deposit returns ERR_LINK_DOWN.",
                result == ERR_LINK_DOWN);

    if (read_balance(bank_db, sender_id, &balance))
    {
        report_test("Offline deposit does not change balance.",
                    fabs(balance - 1200.00) < 0.000001);
    }
    else
    {
        report_test("Could read balance after offline deposit.", 0);
    }


    /* -----------------------------------------------------
       13. Offline withdrawal
       ----------------------------------------------------- */

    result = withdraw(bank_db, sender_id, 50.00, test_link_check);

    report_test("Offline withdrawal returns ERR_LINK_DOWN.",
                result == ERR_LINK_DOWN);

    if (read_balance(bank_db, sender_id, &balance))
    {
        report_test("Offline withdrawal does not change balance.",
                    fabs(balance - 1200.00) < 0.000001);
    }
    else
    {
        report_test("Could read balance after offline withdrawal.", 0);
    }


    /* -----------------------------------------------------
       14. Gateway drops during withdrawal
       ----------------------------------------------------- */

    simulated_link_up = 1;
    callback_calls = 0;
    drop_on_second_check = 1;

    result = withdraw(bank_db, sender_id, 100.00, test_link_check);

    report_test("Gateway drop during withdrawal returns ERR_LINK_DOWN.",
                result == ERR_LINK_DOWN);

    if (read_balance(bank_db, sender_id, &balance))
    {
        report_test("Gateway failure rolls back withdrawal.",
                    fabs(balance - 1200.00) < 0.000001);
    }
    else
    {
        report_test("Could read balance after gateway failure.", 0);
    }

    drop_on_second_check = 0;
    simulated_link_up = 1;


    /* -----------------------------------------------------
       15. Clean up temporary test data
       ----------------------------------------------------- */

    cleanup_success = cleanup_test_data(
        bank_db,
        user_id_1,
        user_id_2,
        sender_id,
        receiver_id,
        savings_id
    );

    report_test("Temporary test data cleaned up.", cleanup_success);


    /* -----------------------------------------------------
       16. Close databases
       ----------------------------------------------------- */

    result = db_close(&bank_db, &atm_db);

    report_test("Both databases closed successfully.", result == OK);

    report_test("Database pointers reset to NULL.",
                bank_db == NULL && atm_db == NULL);


    /* -----------------------------------------------------
       17. Final report
       ----------------------------------------------------- */

    printf("\n====================================\n");
    printf("           TEST SUMMARY\n");
    printf("====================================\n");

    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    if (tests_failed == 0)
    {
        printf("\nALL TRANSACTION TESTS PASSED.\n");
        return 0;
    }

    printf("\nSOME TRANSACTION TESTS FAILED.\n");
    return 1;
}
