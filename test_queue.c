
#include "queue.h"
#include "transaction.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAX_QUEUE_RECORDS 128

static int passed = 0;
static int failed = 0;

static void check(int condition, const char *description)
{
    if (condition)
    {
        printf("[PASS] %s\n", description);
        passed++;
    }
    else
    {
        printf("[FAIL] %s\n", description);
        failed++;
    }
}

/* Creates a test user and one test account. */
static int create_test_account(sqlite3 *bank_db,
                               const char *name,
                               double initial_balance,
                               sqlite3_int64 *account_id)
{
    sqlite3_stmt *statement = NULL;
    sqlite3_int64 user_id;
    int result;

    result = sqlite3_prepare_v2(
        bank_db,
        "INSERT INTO users (name, pin, status, created_at) "
        "VALUES (?, '1234', 'active', '2026-11-24 10:00:00');",
        -1, &statement, NULL
    );

    if (result != SQLITE_OK)
        return ERR_DB;

    if (sqlite3_bind_text(statement, 1, name, -1,
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
        return ERR_DB;

    user_id = sqlite3_last_insert_rowid(bank_db);

    result = sqlite3_prepare_v2(
        bank_db,
        "INSERT INTO accounts "
        "(user_id, account_type, balance, status) "
        "VALUES (?, 'checking', ?, 'active');",
        -1, &statement, NULL
    );

    if (result != SQLITE_OK)
        return ERR_DB;

    if (sqlite3_bind_int64(statement, 1, user_id) != SQLITE_OK ||
        sqlite3_bind_double(statement, 2, initial_balance) != SQLITE_OK)
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
        return ERR_DB;

    *account_id = sqlite3_last_insert_rowid(bank_db);

    return OK;
}

/* Reads an account's current balance. */
static int read_balance(sqlite3 *bank_db,
                        sqlite3_int64 account_id,
                        double *balance)
{
    sqlite3_stmt *statement = NULL;
    int result;

    result = sqlite3_prepare_v2(
        bank_db,
        "SELECT balance FROM accounts WHERE account_id = ?;",
        -1, &statement, NULL
    );

    if (result != SQLITE_OK)
        return ERR_DB;

    if (sqlite3_bind_int64(statement, 1, account_id) != SQLITE_OK)
    {
        sqlite3_finalize(statement);
        return ERR_DB;
    }

    result = sqlite3_step(statement);

    if (result != SQLITE_ROW)
    {
        sqlite3_finalize(statement);
        return ERR_NOT_FOUND;
    }

    *balance = sqlite3_column_double(statement, 0);

    if (sqlite3_finalize(statement) != SQLITE_OK)
        return ERR_DB;

    return OK;
}

/* Checks the synchronization status of a queue record. */
static int queue_record_has_status(const QueueRecord records[],
                                   size_t record_count,
                                   size_t index,
                                   const char *expected_status)
{
    if (index >= record_count)
        return 0;

    return strcmp(records[index].sync_status, expected_status) == 0;
}

int main(void)
{
    sqlite3 *bank_db = NULL;
    sqlite3 *atm_db = NULL;

    sqlite3_int64 sender_id;
    sqlite3_int64 receiver_id;
    sqlite3_int64 poor_account_id;

    QueueRecord records[MAX_QUEUE_RECORDS];

    size_t initial_count = 0;
    size_t record_count = 0;
    size_t synced_count = 0;
    size_t failed_count = 0;

    double balance;
    int result;

    printf("\n====================================\n");
    printf("       QUEUE MODULE TEST\n");
    printf("====================================\n\n");

    /* Step 1: Open and initialize databases. */
    result = db_open(&bank_db, &atm_db);
    check(result == OK, "Both databases opened.");

    if (result != OK)
        return 1;

    result = db_init(bank_db, atm_db);
    check(result == OK, "Database initialization completed.");

    if (result != OK)
        goto cleanup;

    /* Step 2: Make sure there are no old pending records. */
    result = get_queue_status(
        atm_db, records, MAX_QUEUE_RECORDS, &initial_count
    );

    check(result == OK, "Existing queue history retrieved.");

    if (result != OK || initial_count >= MAX_QUEUE_RECORDS)
        goto cleanup;

    for (size_t i = 0; i < initial_count; i++)
    {
        if (strcmp(records[i].sync_status, "pending") == 0)
        {
            printf("\nTest stopped: old pending records exist.\n");
            printf("Please resolve them before running this test.\n");
            goto cleanup;
        }
    }

    /* Step 3: Create three test accounts. */
    result = create_test_account(
        bank_db, "Queue Test Sender", 1000.0, &sender_id
    );

    if (result != OK)
        goto cleanup;

    result = create_test_account(
        bank_db, "Queue Test Receiver", 200.0, &receiver_id
    );

    if (result != OK)
        goto cleanup;

    result = create_test_account(
        bank_db, "Queue Test Poor Account", 10.0, &poor_account_id
    );

    check(result == OK, "Three temporary test accounts created.");

    if (result != OK)
        goto cleanup;

    /* Step 4: Turn the gateway offline. */
    result = set_link_status(0);
    check(result == OK, "Gateway switched offline.");

    /* Step 5: Queue an offline deposit. */
    result = queue_transaction(
        atm_db, sender_id, "deposit", 50.0, 0, 0
    );

    check(result == OK, "Offline deposit successfully queued.");

    /* Step 6: Queue an offline transfer. */
    result = queue_transaction(
        atm_db, sender_id, "transfer", 100.0, receiver_id, 1
    );

    check(result == OK, "Offline transfer successfully queued.");

    /* Step 7: Withdrawals must never be queued. */
    result = queue_transaction(
        atm_db, sender_id, "withdraw", 50.0, 0, 0
    );

    check(result == ERR_INVALID_INPUT,
          "Offline withdrawal is rejected instead of queued.");

    result = get_queue_status(
        atm_db, records, MAX_QUEUE_RECORDS, &record_count
    );

    check(result == OK &&
          record_count == initial_count + 2,
          "Only the deposit and transfer were added.");

    check(queue_record_has_status(
              records, record_count, initial_count, "pending"),
          "Queued deposit initially has pending status.");

    check(queue_record_has_status(
              records, record_count, initial_count + 1, "pending"),
          "Queued transfer initially has pending status.");

    /* Step 8: Bring the gateway online and synchronize. */
    result = set_link_status(1);
    check(result == OK, "Gateway switched online.");

    result = sync_pending_queue(
        bank_db, atm_db, &synced_count, &failed_count
    );

    check(result == OK, "Queue synchronization completed.");
    check(synced_count == 2 && failed_count == 0,
          "Both queued transactions synchronized successfully.");

    /* Verify the balances after synchronization. */
    result = read_balance(bank_db, sender_id, &balance);

    check(result == OK && fabs(balance - 950.0) < 0.001,
          "Sender balance is correct after deposit and transfer.");

    result = read_balance(bank_db, receiver_id, &balance);

    check(result == OK && fabs(balance - 300.0) < 0.001,
          "Receiver balance is correct after transfer.");

    result = get_queue_status(
        atm_db, records, MAX_QUEUE_RECORDS, &record_count
    );

    check(result == OK &&
          queue_record_has_status(
              records, record_count, initial_count, "synced"),
          "Deposit queue record is marked synced.");

    check(queue_record_has_status(
              records, record_count, initial_count + 1, "synced"),
          "Transfer queue record is marked synced.");

    /* Step 9: Queue a transfer that cannot be completed. */
    set_link_status(0);

    result = queue_transaction(
        atm_db, poor_account_id, "transfer", 100.0, receiver_id, 1
    );

    check(result == OK, "Insufficient-funds transfer was queued.");

    set_link_status(1);

    result = sync_pending_queue(
        bank_db, atm_db, &synced_count, &failed_count
    );

    check(result == OK, "Synchronization handled the failed transfer.");
    check(synced_count == 0 && failed_count == 1,
          "Insufficient-funds transfer counted as failed.");

    result = get_queue_status(
        atm_db, records, MAX_QUEUE_RECORDS, &record_count
    );

    check(result == OK &&
          queue_record_has_status(
              records, record_count, initial_count + 2, "failed"),
          "Failed transfer remains in history with failed status.");

    /* Step 10: Test a gateway drop during synchronization. */
    set_link_status(0);

    result = queue_transaction(
        atm_db, sender_id, "deposit", 25.0, 0, 0
    );

    check(result == OK, "Another offline deposit was queued.");

    set_link_status(1);

    result = simulate_link_drop_during_next_transaction();

    check(result == OK, "One-shot gateway failure was armed.");

    result = sync_pending_queue(
        bank_db, atm_db, &synced_count, &failed_count
    );

    check(result == ERR_LINK_DOWN,
          "Gateway drop interrupts synchronization.");

    check(synced_count == 0 && failed_count == 0,
          "Interrupted transaction is not counted as synced or failed.");

    result = get_queue_status(
        atm_db, records, MAX_QUEUE_RECORDS, &record_count
    );

    check(result == OK &&
          queue_record_has_status(
              records, record_count, initial_count + 3, "pending"),
          "Interrupted deposit remains pending.");

    result = read_balance(bank_db, sender_id, &balance);

    check(result == OK && fabs(balance - 950.0) < 0.001,
          "Interrupted deposit does not change the bank balance.");

    /* Step 11: Retry after the gateway failure. */
    result = sync_pending_queue(
        bank_db, atm_db, &synced_count, &failed_count
    );

    check(result == OK && synced_count == 1 && failed_count == 0,
          "Pending deposit synchronizes successfully on retry.");

    result = read_balance(bank_db, sender_id, &balance);

    check(result == OK && fabs(balance - 975.0) < 0.001,
          "Sender balance increases after successful retry.");

    result = get_queue_status(
        atm_db, records, MAX_QUEUE_RECORDS, &record_count
    );

    check(result == OK &&
          queue_record_has_status(
              records, record_count, initial_count + 3, "synced"),
          "Retried deposit is marked synced.");

    /* Step 12: Test callback cleanup after an early failure. */

    set_link_status(0);

    result = queue_transaction(
        atm_db, poor_account_id, "transfer", 100.0, receiver_id, 1
    );

    check(result == OK,
          "Another insufficient-funds transfer was queued.");

    result = queue_transaction(
        atm_db, sender_id, "deposit", 10.0, 0, 0
    );

    check(result == OK,
          "A valid deposit was queued after the failed transfer.");

    set_link_status(1);

    result = simulate_link_drop_during_next_transaction();

    check(result == OK,
          "One-shot gateway failure was armed.");

    result = sync_pending_queue(
        bank_db, atm_db, &synced_count, &failed_count
    );

    check(result == OK,
          "Synchronization continued after an early business failure.");

    check(synced_count == 1 && failed_count == 1,
          "First transaction failed and second transaction succeeded.");

    result = read_balance(bank_db, sender_id, &balance);

    check(result == OK && fabs(balance - 985.0) < 0.001,
          "Second transaction completed without an unexpected gateway failure.");

    result = get_queue_status(
        atm_db, records, MAX_QUEUE_RECORDS, &record_count
    );

    check(result == OK &&
          queue_record_has_status(
              records, record_count, initial_count + 4, "failed"),
          "Early insufficient-funds transaction is marked failed.");

    check(queue_record_has_status(
              records, record_count, initial_count + 5, "synced"),
          "Following deposit is marked synced.");

cleanup:

    /* Restore the default gateway state. */
    set_link_status(1);

    if (bank_db != NULL || atm_db != NULL)
    {
        result = db_close(&bank_db, &atm_db);
        check(result == OK, "Both databases closed.");
    }

    check(bank_db == NULL && atm_db == NULL,
          "Database pointers reset to NULL.");

    printf("\n====================================\n");
    printf("           TEST SUMMARY\n");
    printf("====================================\n");
    printf("Passed: %d\n", passed);
    printf("Failed: %d\n", failed);

    if (failed == 0)
        printf("\nALL QUEUE TESTS PASSED.\n");
    else
        printf("\nSOME QUEUE TESTS FAILED.\n");

    return failed == 0 ? 0 : 1;
}
