#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <stddef.h>
#include "db.h"
#include "status.h"


typedef int (*TransactionLinkCheck)(void);

/* One completed transaction returned by get_transaction_history(). */
typedef struct
{
    sqlite3_int64 txn_id;
    sqlite3_int64 account_id;
    char type[20];
    double amount;
    sqlite3_int64 related_account_id;
    int has_related_account;
    char timestamp[20]; 
} TransactionRecord;

int deposit(sqlite3 *bank_db,
            sqlite3_int64 account_id,
            double amount,
            TransactionLinkCheck link_check);

int withdraw(sqlite3 *bank_db,
             sqlite3_int64 account_id,
             double amount,
             TransactionLinkCheck link_check);

int transfer(sqlite3 *bank_db,
             sqlite3_int64 sender_account_id,
             sqlite3_int64 receiver_account_id,
             double amount,
             TransactionLinkCheck link_check);

int apply_interest(sqlite3 *bank_db,
                   sqlite3_int64 account_id,
                   double interest_rate_percent);


int get_transaction_history(sqlite3 *bank_db,
                            sqlite3_int64 account_id,
                            TransactionRecord records[],
                            size_t capacity,
                            size_t *record_count);

#endif
