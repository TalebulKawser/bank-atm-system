
#include "queue.h"
#include "transaction.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Simulated gateway state. The gateway starts online. */
static int gateway_online = 1;

/* One-shot failure simulation state. */
static int break_during_next_transaction = 0;
static int gateway_check_count = 0;


/* =========================================================
   INTERNAL HELPER FUNCTIONS
   ========================================================= */

/* Generates a timestamp in YYYY-MM-DD HH:MM:SS format. */
static int generate_queue_timestamp(char timestamp[], size_t capacity)
{
    time_t current_time;
    struct tm *local_time;

    if (timestamp == NULL || capacity < 20)
        return ERR_INVALID_INPUT;

    current_time = time(NULL);
    local_time = localtime(&current_time);

    if (local_time == NULL)
        return ERR_DB;

    if (strftime(timestamp, capacity, "%Y-%m-%d %H:%M:%S",
                 local_time) == 0)
        return ERR_DB;

    return OK;
}

/* Updates a queue record from pending to synced or failed. */
static int update_queue_status(sqlite3 *atm_db,
                               sqlite3_int64 queue_id,
                               const char *new_status)
{
    sqlite3_stmt *statement = NULL;
    int result;

    if (atm_db == NULL || queue_id <= 0 || new_status == NULL)
        return ERR_INVALID_INPUT;

    if (strcmp(new_status, "synced") != 0 &&
        strcmp(new_status, "failed") != 0)
        return ERR_INVALID_INPUT;

    result = sqlite3_prepare_v2(
        atm_db,
        "UPDATE pending_queue "
        "SET sync_status = ? "
        "WHERE queue_id = ? AND sync_status = 'pending';",
        -1,
        &statement,
        NULL
    );

    if (result != SQLITE_OK)
        return ERR_DB;

    if (sqlite3_bind_text(statement, 1, new_status, -1,
                          SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int64(statement, 2, queue_id) != SQLITE_OK)
    {
        sqlite3_finalize(statement);
        return ERR_DB;
    }

    result = sqlite3_step(statement);

    if (result != SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        return ERR_DB;
    }

    if (sqlite3_changes(atm_db) != 1)
    {
        sqlite3_finalize(statement);
        return ERR_NOT_FOUND;
    }

    if (sqlite3_finalize(statement) != SQLITE_OK)
        return ERR_DB;

    return OK;
}


/* =========================================================
   GATEWAY MANAGEMENT
   ========================================================= */

/* Returns 1 if the gateway is online, otherwise 0. */
int is_link_up(void)
{
    if (!gateway_online)
        return 0;

    if (break_during_next_transaction)
    {
        gateway_check_count++;

        /*
         * First check: connection is available.
         * Second check: simulate a connection failure.
         */
        if (gateway_check_count >= 2)
        {
            break_during_next_transaction = 0;
            gateway_check_count = 0;
            return 0;
        }
    }

    return 1;
}

/* Manually changes the simulated gateway status. */
int set_link_status(int is_up)
{
    if (is_up != 0 && is_up != 1)
        return ERR_INVALID_INPUT;

    gateway_online = is_up;
    gateway_check_count = 0;

    return OK;
}

/* Arms a one-time gateway failure during the next transaction. */
int simulate_link_drop_during_next_transaction(void)
{
    if (!gateway_online)
        return ERR_LINK_DOWN;

    break_during_next_transaction = 1;
    gateway_check_count = 0;

    return OK;
}

/* Resets the callback counter before starting a transaction. */
void reset_link_check_sequence(void)
{
    gateway_check_count = 0;
}

/*
 * Clears any unused one-shot failure after a transaction finishes.
 * This must be called even when a transaction fails early.
 */
void end_link_check_sequence(void)
{
    gateway_check_count = 0;
    break_during_next_transaction = 0;
}


/* =========================================================
   OFFLINE QUEUE STORAGE
   ========================================================= */

/* Stores an offline deposit or transfer in atm.db. */
int queue_transaction(sqlite3 *atm_db,
                      sqlite3_int64 account_id,
                      const char *type,
                      double amount,
                      sqlite3_int64 related_account_id,
                      int has_related_account)
{
    sqlite3_stmt *statement = NULL;
    char timestamp[20];
    int result;

    if (atm_db == NULL || account_id <= 0 || type == NULL)
        return ERR_INVALID_INPUT;

    if (gateway_online)
        return ERR_INVALID_INPUT;

    if (!isfinite(amount) || amount <= 0.0)
        return ERR_INVALID_AMOUNT;

    if (strcmp(type, "deposit") != 0 &&
        strcmp(type, "transfer") != 0)
        return ERR_INVALID_INPUT;

    if (strcmp(type, "deposit") == 0)
    {
        if (has_related_account != 0)
            return ERR_INVALID_INPUT;
    }
    else
    {
        if (has_related_account != 1 ||
            related_account_id <= 0 ||
            related_account_id == account_id)
            return ERR_INVALID_ACCOUNT;
    }

    if (has_related_account != 0 && has_related_account != 1)
        return ERR_INVALID_INPUT;

    if (generate_queue_timestamp(timestamp, sizeof(timestamp)) != OK)
        return ERR_DB;

    result = sqlite3_prepare_v2(
        atm_db,
        "INSERT INTO pending_queue "
        "(account_id, type, amount, related_account_id, queued_at, sync_status) "
        "VALUES (?, ?, ?, ?, ?, 'pending');",
        -1,
        &statement,
        NULL
    );

    if (result != SQLITE_OK)
        return ERR_DB;

    if (sqlite3_bind_int64(statement, 1, account_id) != SQLITE_OK ||
        sqlite3_bind_text(statement, 2, type, -1,
                          SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_double(statement, 3, amount) != SQLITE_OK)
    {
        sqlite3_finalize(statement);
        return ERR_DB;
    }

    if (has_related_account)
        result = sqlite3_bind_int64(statement, 4, related_account_id);
    else
        result = sqlite3_bind_null(statement, 4);

    if (result != SQLITE_OK ||
        sqlite3_bind_text(statement, 5, timestamp, -1,
                          SQLITE_TRANSIENT) != SQLITE_OK)
    {
        sqlite3_finalize(statement);
        return ERR_DB;
    }

    result = sqlite3_step(statement);

    if (result != SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        return ERR_DB;
    }

    if (sqlite3_finalize(statement) != SQLITE_OK)
        return ERR_DB;

    return OK;
}


/* =========================================================
   QUEUE SYNCHRONIZATION
   ========================================================= */

/*
 * Processes pending entries in oldest-first order.
 * Successful entries become synced.
 * Business failures become failed.
 * Gateway failures leave the entry pending.
 */
int sync_pending_queue(sqlite3 *bank_db,
                       sqlite3 *atm_db,
                       size_t *synced_count,
                       size_t *failed_count)
{
    sqlite3_stmt *statement = NULL;
    QueueRecord *pending_records = NULL;

    size_t pending_count = 0;
    size_t capacity = 0;
    size_t i;

    int result;
    int final_status = OK;

    if (bank_db == NULL || atm_db == NULL ||
        synced_count == NULL || failed_count == NULL)
        return ERR_INVALID_INPUT;

    *synced_count = 0;
    *failed_count = 0;

    if (!gateway_online)
        return ERR_LINK_DOWN;

    /*
     * Retrieve all pending entries in queue order.
     * We collect them first so that the SELECT statement
     * can be finalized before updating queue statuses.
     */
    result = sqlite3_prepare_v2(
        atm_db,
        "SELECT queue_id, account_id, type, amount, "
        "related_account_id, queued_at "
        "FROM pending_queue "
        "WHERE sync_status = 'pending' "
        "ORDER BY queue_id ASC;",
        -1,
        &statement,
        NULL
    );

    if (result != SQLITE_OK)
        return ERR_DB;

    while ((result = sqlite3_step(statement)) == SQLITE_ROW)
    {
        QueueRecord *expanded_records;
        const unsigned char *type_text;
        const unsigned char *timestamp_text;

        if (pending_count == capacity)
        {
            size_t new_capacity = (capacity == 0) ? 8 : capacity * 2;

            expanded_records = realloc(
                pending_records,
                new_capacity * sizeof(QueueRecord)
            );

            if (expanded_records == NULL)
            {
                sqlite3_finalize(statement);
                free(pending_records);
                return ERR_MEMORY;
            }

            pending_records = expanded_records;
            capacity = new_capacity;
        }

        pending_records[pending_count].queue_id =
            sqlite3_column_int64(statement, 0);

        pending_records[pending_count].account_id =
            sqlite3_column_int64(statement, 1);

        type_text = sqlite3_column_text(statement, 2);

        snprintf(
            pending_records[pending_count].type,
            sizeof(pending_records[pending_count].type),
            "%s",
            type_text != NULL ? (const char *)type_text : ""
        );

        pending_records[pending_count].amount =
            sqlite3_column_double(statement, 3);

        pending_records[pending_count].has_related_account =
            sqlite3_column_type(statement, 4) != SQLITE_NULL;

        pending_records[pending_count].related_account_id =
            pending_records[pending_count].has_related_account
                ? sqlite3_column_int64(statement, 4)
                : 0;

        timestamp_text = sqlite3_column_text(statement, 5);

        snprintf(
            pending_records[pending_count].queued_at,
            sizeof(pending_records[pending_count].queued_at),
            "%s",
            timestamp_text != NULL
                ? (const char *)timestamp_text : ""
        );

        pending_count++;
    }

    if (result != SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        free(pending_records);
        return ERR_DB;
    }

    if (sqlite3_finalize(statement) != SQLITE_OK)
    {
        free(pending_records);
        return ERR_DB;
    }

    /* Replay each pending transaction, oldest first. */
    for (i = 0; i < pending_count; i++)
    {
        int transaction_result;
        int status_result;

        if (!gateway_online)
        {
            final_status = ERR_LINK_DOWN;
            break;
        }

        {
            int attempt;

            /* A one-time simulated drop gets one automatic retry. */
            for (attempt = 0; attempt < 2; attempt++)
            {
                reset_link_check_sequence();

                if (strcmp(pending_records[i].type, "deposit") == 0)
                {
                    transaction_result = deposit(
                        bank_db, pending_records[i].account_id,
                        pending_records[i].amount, is_link_up
                    );
                }
                else if (strcmp(pending_records[i].type, "transfer") == 0 &&
                         pending_records[i].has_related_account)
                {
                    transaction_result = transfer(
                        bank_db, pending_records[i].account_id,
                        pending_records[i].related_account_id,
                        pending_records[i].amount, is_link_up
                    );
                }
                else
                {
                    transaction_result = ERR_INVALID_INPUT;
                }

                /* Always clear a one-shot fault after each attempt. */
                end_link_check_sequence();

                if (transaction_result != ERR_LINK_DOWN || !gateway_online)
                    break;

                /* The gateway is still administratively online: this was
                   the one-time fault, so retry this same queue item once. */
            }
        }

        /* A real/persistent outage leaves this and later items pending. */
        if (transaction_result == ERR_LINK_DOWN)
        {
            final_status = ERR_LINK_DOWN;
            break;
        }

        if (transaction_result == OK)
        {
            status_result = update_queue_status(
                atm_db,
                pending_records[i].queue_id,
                "synced"
            );

            if (status_result != OK)
            {
                final_status = status_result;
                break;
            }

            (*synced_count)++;
        }
        else
        {
            status_result = update_queue_status(
                atm_db,
                pending_records[i].queue_id,
                "failed"
            );

            if (status_result != OK)
            {
                final_status = status_result;
                break;
            }

            (*failed_count)++;
        }
    }

    free(pending_records);

    return final_status;
}


/* =========================================================
   QUEUE HISTORY
   ========================================================= */

/*
 * Retrieves queue records, including pending, synced, and failed.
 * Records are returned in oldest-first order.
 */
int get_queue_status(sqlite3 *atm_db,
                     QueueRecord records[],
                     size_t capacity,
                     size_t *record_count)
{
    sqlite3_stmt *statement = NULL;
    size_t count = 0;
    int result;

    if (atm_db == NULL || record_count == NULL ||
        (capacity > 0 && records == NULL))
        return ERR_INVALID_INPUT;

    *record_count = 0;

    result = sqlite3_prepare_v2(
        atm_db,
        "SELECT queue_id, account_id, type, amount, "
        "related_account_id, queued_at, sync_status "
        "FROM pending_queue "
        "ORDER BY queue_id ASC;",
        -1,
        &statement,
        NULL
    );

    if (result != SQLITE_OK)
        return ERR_DB;

    while ((result = sqlite3_step(statement)) == SQLITE_ROW)
    {
        const unsigned char *type_text;
        const unsigned char *timestamp_text;
        const unsigned char *status_text;

        if (count < capacity)
        {
            records[count].queue_id =
                sqlite3_column_int64(statement, 0);

            records[count].account_id =
                sqlite3_column_int64(statement, 1);

            type_text = sqlite3_column_text(statement, 2);

            snprintf(
                records[count].type,
                sizeof(records[count].type),
                "%s",
                type_text != NULL ? (const char *)type_text : ""
            );

            records[count].amount =
                sqlite3_column_double(statement, 3);

            records[count].has_related_account =
                sqlite3_column_type(statement, 4) != SQLITE_NULL;

            records[count].related_account_id =
                records[count].has_related_account
                    ? sqlite3_column_int64(statement, 4)
                    : 0;

            timestamp_text = sqlite3_column_text(statement, 5);

            snprintf(
                records[count].queued_at,
                sizeof(records[count].queued_at),
                "%s",
                timestamp_text != NULL
                    ? (const char *)timestamp_text : ""
            );

            status_text = sqlite3_column_text(statement, 6);

            snprintf(
                records[count].sync_status,
                sizeof(records[count].sync_status),
                "%s",
                status_text != NULL ? (const char *)status_text : ""
            );

            count++;
        }
    }

    if (result != SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        return ERR_DB;
    }

    if (sqlite3_finalize(statement) != SQLITE_OK)
        return ERR_DB;

    *record_count = count;

    return OK;
}
