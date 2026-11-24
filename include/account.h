#ifndef ACCOUNT_H
#define ACCOUNT_H

#include "db.h"
#include "status.h"

/*
 * Account/user module.
 * All database-using functions receive the already-open bank database.
 * This keeps the module consistent with db_open() and transaction.c.
 */

/* One account and one node in the in-session linked list. */
typedef struct AccountNode
{
    sqlite3_int64 account_id;
    sqlite3_int64 user_id;
    char owner_name[51];
    char account_type[10]; /* "savings" or "checking" */
    double balance;
    char status[10];        /* "active", "frozen" or "closed" */
    struct AccountNode *next;
} AccountNode;

/* Creates a customer. PIN must be exactly four digits. */
int create_user(sqlite3 *bank_db,
                const char *name,
                const char *pin,
                sqlite3_int64 *out_user_id);

/* Opens a savings or checking account with an initial balance of zero. */
int create_account(sqlite3 *bank_db,
                   sqlite3_int64 user_id,
                   const char *account_type,
                   sqlite3_int64 *out_account_id);

/* Creates the customer and their first account as one atomic operation. */
int register_customer(sqlite3 *bank_db,
                      const char *name,
                      const char *pin,
                      const char *account_type,
                      sqlite3_int64 *out_user_id,
                      sqlite3_int64 *out_account_id);

/* Sets *out_has to 1 if this user has the account type, otherwise 0. */
int has_account_type(sqlite3 *bank_db,
                     sqlite3_int64 user_id,
                     const char *account_type,
                     int *out_has);

/* Login by account number and PIN. Unknown account and wrong PIN both
 * return ERR_AUTH_FAILED, so the result does not reveal which was wrong. */
int authenticate_user(sqlite3 *bank_db,
                      sqlite3_int64 account_id,
                      const char *pin,
                      sqlite3_int64 *out_user_id);

int get_balance(sqlite3 *bank_db,
                sqlite3_int64 account_id,
                double *out_balance);

/* Admin-only account status operations. */
int freeze_account(sqlite3 *bank_db, sqlite3_int64 account_id);
int unfreeze_account(sqlite3 *bank_db, sqlite3_int64 account_id);
int close_account(sqlite3 *bank_db, sqlite3_int64 account_id);
int reopen_account(sqlite3 *bank_db, sqlite3_int64 account_id);

/* Admin tools. The caller owns the returned list and must free it. */
int search_account(sqlite3 *bank_db,
                   const char *query,
                   AccountNode **out_head);

int sort_accounts(sqlite3 *bank_db,
                  const char *by_field,
                  AccountNode **out_head);

/* Counts and total balance across all accounts. */
int get_bank_summary(sqlite3 *bank_db,
                     sqlite3_int64 *out_customers,
                     sqlite3_int64 *out_accounts,
                     double *out_total_balance);

/* In-session linked list of one user's accounts. */
int load_user_accounts(sqlite3 *bank_db,
                       sqlite3_int64 user_id,
                       AccountNode **out_head);

AccountNode *find_account_in_list(AccountNode *head,
                                  sqlite3_int64 account_id);

/* Frees every node and sets *head to NULL. */
void free_account_list(AccountNode **head);

#endif /* ACCOUNT_H */
