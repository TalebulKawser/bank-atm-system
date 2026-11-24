
#ifndef QUEUE_H
#define QUEUE_H

#include <stddef.h>
#include "db.h"
#include "status.h"

/* One queue entry returned by get_queue_status(). */
typedef struct
{
    sqlite3_int64 queue_id;
    sqlite3_int64 account_id;
    char type[20];
    double amount;
    sqlite3_int64 related_account_id;
    int has_related_account;
    char queued_at[20];
    char sync_status[10];
} QueueRecord;

/* Returns 1 if the simulated gateway is online, otherwise 0. */
int is_link_up(void);

/* Manually changes the simulated gateway status. */
int set_link_status(int is_up);

/* Simulates the gateway dropping during the next transaction. */
int simulate_link_drop_during_next_transaction(void);

/* Stores an offline deposit or transfer in atm.db. */
int queue_transaction(sqlite3 *atm_db,
                      sqlite3_int64 account_id,
                      const char *type,
                      double amount,
                      sqlite3_int64 related_account_id,
                      int has_related_account);

/* Replays pending queue entries in oldest-first order. */
int sync_pending_queue(sqlite3 *bank_db,
                       sqlite3 *atm_db,
                       size_t *synced_count,
                       size_t *failed_count);

/* Retrieves queue records, including pending, synced, and failed entries. */
int get_queue_status(sqlite3 *atm_db,
                     QueueRecord records[],
                     size_t capacity,
                     size_t *record_count);

void reset_link_check_sequence(void);
void end_link_check_sequence(void);

#endif
