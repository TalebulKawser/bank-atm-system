/* account.c - users and accounts as records.
 * Owner: Rakibul Hasan Md Ismail
 *
 * Rules used in this file:
 *   - All SQL uses prepared statements (never paste user text into SQL).
 *   - Every sqlite3_stmt is finalized on every path out of a function.
 *   - Nothing here prints; only ui.c talks to the screen.
 *
 * The caller supplies the already-open bank database connection. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "account.h"
#include "db.h"


/* ------------------------------------------------------------------ */
/* Small private helpers                                               */
/* ------------------------------------------------------------------ */

static int is_valid_pin(const char *pin)
{
    if (pin == NULL || strlen(pin) != 4) return 0;
    for (int i = 0; i < 4; i++)
        if (!isdigit((unsigned char)pin[i])) return 0;
    return 1;
}

static int is_valid_type(const char *t)
{
    return t != NULL &&
           (strcmp(t, "savings") == 0 || strcmp(t, "checking") == 0);
}

static int is_all_digits(const char *s)
{
    if (s == NULL || s[0] == '\0') return 0;
    for (int i = 0; s[i] != '\0'; i++)
        if (!isdigit((unsigned char)s[i])) return 0;
    return 1;
}

/* Safe copy of SQLite text (which can be NULL) into a fixed-size buffer. */
static void copy_text(char *dest, size_t size, const unsigned char *src)
{
    snprintf(dest, size, "%s", src != NULL ? (const char *)src : "");
}

/* ------------------------------------------------------------------ */
/* Users and accounts                                                  */
/* ------------------------------------------------------------------ */

int create_user(sqlite3 *bank_db, const char *name, const char *pin, sqlite3_int64 *out_user_id)
{
    if (bank_db == NULL || name == NULL || out_user_id == NULL) return ERR_INVALID_INPUT;
    if (name[0] == '\0' || strlen(name) > 50) return ERR_INVALID_INPUT;
    if (!is_valid_pin(pin)) return ERR_INVALID_INPUT;

    sqlite3 *db = bank_db;
    sqlite3_stmt *st = NULL;
    const char *sql =
        "INSERT INTO users (name, pin, status, created_at) "
        "VALUES (?, ?, 'active', datetime('now'))";

    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return ERR_DB;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, pin,  -1, SQLITE_STATIC);

    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return ERR_DB;

    *out_user_id = sqlite3_last_insert_rowid(db);
    return OK;
}

int has_account_type(sqlite3 *bank_db, sqlite3_int64 user_id, const char *account_type, int *out_has)
{
    if (bank_db == NULL || user_id <= 0 || !is_valid_type(account_type) || out_has == NULL) return ERR_INVALID_INPUT;

    sqlite3_stmt *st = NULL;
    const char *sql =
        "SELECT COUNT(*) FROM accounts WHERE user_id = ? AND account_type = ?";

    if (sqlite3_prepare_v2(bank_db, sql, -1, &st, NULL) != SQLITE_OK) return ERR_DB;
    sqlite3_bind_int64(st, 1, user_id);
    sqlite3_bind_text(st, 2, account_type, -1, SQLITE_STATIC);

    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) *out_has = (sqlite3_column_int64(st, 0) > 0);
    sqlite3_finalize(st);
    return (rc == SQLITE_ROW) ? OK : ERR_DB;
}

int create_account(sqlite3 *bank_db, sqlite3_int64 user_id, const char *account_type, sqlite3_int64 *out_account_id)
{
    if (bank_db == NULL || user_id <= 0 || !is_valid_type(account_type) || out_account_id == NULL)
        return ERR_INVALID_INPUT;

    /* Friendly pre-check; the database unique index is the final safeguard. */
    int has = 0;
    int rc = has_account_type(bank_db, user_id, account_type, &has);
    if (rc != OK) return rc;
    if (has) return ERR_DUPLICATE_TYPE;

    sqlite3 *db = bank_db;
    sqlite3_stmt *st = NULL;
    const char *sql =
        "INSERT INTO accounts (user_id, account_type, balance, status) "
        "VALUES (?, ?, 0.0, 'active')";

    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return ERR_DB;
    sqlite3_bind_int64(st, 1, user_id);
    sqlite3_bind_text(st, 2, account_type, -1, SQLITE_STATIC);

    rc = sqlite3_step(st);
    int extended_rc = sqlite3_extended_errcode(db);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
        if (extended_rc == SQLITE_CONSTRAINT_UNIQUE) return ERR_DUPLICATE_TYPE;
        return ERR_DB;
    }

    *out_account_id = sqlite3_last_insert_rowid(db);
    return OK;
}

int register_customer(sqlite3 *bank_db, const char *name, const char *pin, const char *account_type,
                       sqlite3_int64 *out_user_id, sqlite3_int64 *out_account_id)
{
    if (bank_db == NULL || name == NULL || name[0] == '\0' || strlen(name) > 50 ||
        !is_valid_pin(pin) || !is_valid_type(account_type) ||
        out_user_id == NULL || out_account_id == NULL)
        return ERR_INVALID_INPUT;

    sqlite3 *db = bank_db;
    if (sqlite3_exec(db, "BEGIN", NULL, NULL, NULL) != SQLITE_OK) return ERR_DB;

    sqlite3_int64 uid = 0, aid = 0;
    int rc = create_user(bank_db, name, pin, &uid);
    if (rc == OK) rc = create_account(bank_db, uid, account_type, &aid);

    if (rc != OK) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);   /* undo everything */
        return rc;
    }
    if (sqlite3_exec(db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
        return ERR_DB;
    }

    *out_user_id = uid;
    *out_account_id = aid;
    return OK;
}

int authenticate_user(sqlite3 *bank_db, sqlite3_int64 account_id, const char *pin, sqlite3_int64 *out_user_id)
{
    if (bank_db == NULL || account_id <= 0 || pin == NULL || out_user_id == NULL) return ERR_INVALID_INPUT;

    sqlite3_stmt *st = NULL;
    const char *sql =
        "SELECT u.user_id, u.pin FROM accounts a "
        "JOIN users u ON a.user_id = u.user_id WHERE a.account_id = ?";

    if (sqlite3_prepare_v2(bank_db, sql, -1, &st, NULL) != SQLITE_OK) return ERR_DB;
    sqlite3_bind_int64(st, 1, account_id);

    int result = ERR_AUTH_FAILED;   /* unknown account and wrong PIN look the same */
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        const char *stored = (const char *)sqlite3_column_text(st, 1);
        if (stored != NULL && strcmp(stored, pin) == 0) {
            *out_user_id = sqlite3_column_int64(st, 0);
            result = OK;
        }
    } else if (rc != SQLITE_DONE) {
        result = ERR_DB;
    }
    sqlite3_finalize(st);           /* only AFTER we finished using 'stored' */
    return result;
}

int get_balance(sqlite3 *bank_db, sqlite3_int64 account_id, double *out_balance)
{
    if (bank_db == NULL || account_id <= 0 || out_balance == NULL) return ERR_INVALID_INPUT;

    sqlite3_stmt *st = NULL;
    const char *sql = "SELECT balance FROM accounts WHERE account_id = ?";
    if (sqlite3_prepare_v2(bank_db, sql, -1, &st, NULL) != SQLITE_OK) return ERR_DB;
    sqlite3_bind_int64(st, 1, account_id);

    int rc = sqlite3_step(st);
    int result;
    if (rc == SQLITE_ROW) {
        *out_balance = sqlite3_column_double(st, 0);
        result = OK;
    } else if (rc == SQLITE_DONE) {
        result = ERR_NOT_FOUND;
    } else {
        result = ERR_DB;
    }
    sqlite3_finalize(st);
    return result;
}

static int set_account_status(sqlite3 *bank_db, sqlite3_int64 account_id, const char *new_status)
{
    sqlite3 *db = bank_db;
    sqlite3_stmt *st = NULL;
    const char *sql = "UPDATE accounts SET status = ? WHERE account_id = ?";

    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return ERR_DB;
    sqlite3_bind_text(st, 1, new_status, -1, SQLITE_STATIC);
    sqlite3_bind_int64(st, 2, account_id);

    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return ERR_DB;
    return (sqlite3_changes(db) == 0) ? ERR_NOT_FOUND : OK;
}

int freeze_account(sqlite3 *bank_db, sqlite3_int64 account_id) { return set_account_status(bank_db, account_id, "frozen"); }
int close_account(sqlite3 *bank_db, sqlite3_int64 account_id)  { return set_account_status(bank_db, account_id, "closed"); }

/* ------------------------------------------------------------------ */
/* Linked list: build, find, free                                      */
/* ------------------------------------------------------------------ */

void free_account_list(AccountNode **head)
{
    if (head == NULL) return;
    AccountNode *p = *head;
    while (p != NULL) {
        AccountNode *next = p->next;   /* save BEFORE freeing */
        free(p);
        p = next;
    }
    *head = NULL;
}

AccountNode *find_account_in_list(AccountNode *head, sqlite3_int64 account_id)
{
    for (AccountNode *p = head; p != NULL; p = p->next)
        if (p->account_id == account_id) return p;
    return NULL;
}

/* Every list query selects these six columns in this order. */
#define ACCOUNT_SELECT \
    "SELECT a.account_id, a.user_id, u.name, a.account_type, a.balance, a.status " \
    "FROM accounts a JOIN users u ON a.user_id = u.user_id "

/* Runs a prepared statement and turns each result row into a list node. */
static int build_list(sqlite3_stmt *st, AccountNode **head)
{
    AccountNode *tail = NULL;
    *head = NULL;

    int rc;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        AccountNode *n = malloc(sizeof(AccountNode));
        if (n == NULL) {
            free_account_list(head);
            return ERR_MEMORY;
        }
        n->account_id = sqlite3_column_int64(st, 0);
        n->user_id    = sqlite3_column_int64(st, 1);
        copy_text(n->owner_name,   sizeof n->owner_name,   sqlite3_column_text(st, 2));
        copy_text(n->account_type, sizeof n->account_type, sqlite3_column_text(st, 3));
        n->balance    = sqlite3_column_double(st, 4);
        copy_text(n->status,       sizeof n->status,       sqlite3_column_text(st, 5));
        n->next = NULL;

        if (tail != NULL) tail->next = n; else *head = n;
        tail = n;
    }

    if (rc != SQLITE_DONE) {
        free_account_list(head);
        return ERR_DB;
    }
    return OK;
}

int load_user_accounts(sqlite3 *bank_db, sqlite3_int64 user_id, AccountNode **out_head)
{
    if (bank_db == NULL || user_id <= 0 || out_head == NULL) return ERR_INVALID_INPUT;

    sqlite3_stmt *st = NULL;
    const char *sql = ACCOUNT_SELECT "WHERE a.user_id = ? ORDER BY a.account_id";
    if (sqlite3_prepare_v2(bank_db, sql, -1, &st, NULL) != SQLITE_OK) return ERR_DB;
    sqlite3_bind_int64(st, 1, user_id);

    int rc = build_list(st, out_head);
    sqlite3_finalize(st);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Admin tools                                                         */
/* ------------------------------------------------------------------ */

int search_account(sqlite3 *bank_db, const char *query, AccountNode **out_head)
{
    if (bank_db == NULL || query == NULL || query[0] == '\0' || out_head == NULL) return ERR_INVALID_INPUT;

    sqlite3_stmt *st = NULL;

    if (is_all_digits(query)) {                         /* by account number */
        errno = 0;
        char *end = NULL;
        long long parsed_id = strtoll(query, &end, 10);
        if (errno == ERANGE || end == query || *end != '\0' || parsed_id <= 0)
            return ERR_INVALID_INPUT;

        const char *sql = ACCOUNT_SELECT "WHERE a.account_id = ?";
        if (sqlite3_prepare_v2(bank_db, sql, -1, &st, NULL) != SQLITE_OK) return ERR_DB;
        sqlite3_bind_int64(st, 1, (sqlite3_int64)parsed_id);
    } else {                                            /* by name, partial match */
        char pattern[80];
        snprintf(pattern, sizeof pattern, "%%%.60s%%", query);
        const char *sql = ACCOUNT_SELECT "WHERE u.name LIKE ? ORDER BY u.name";
        if (sqlite3_prepare_v2(bank_db, sql, -1, &st, NULL) != SQLITE_OK) return ERR_DB;
        sqlite3_bind_text(st, 1, pattern, -1, SQLITE_TRANSIENT); /* copy: pattern is local */
    }

    int rc = build_list(st, out_head);
    sqlite3_finalize(st);
    return rc;
}

int sort_accounts(sqlite3 *bank_db, const char *by_field, AccountNode **out_head)
{
    if (bank_db == NULL || by_field == NULL || out_head == NULL) return ERR_INVALID_INPUT;

    /* Column names cannot be bound with '?', so pick one of fixed strings. */
    const char *sql;
    if      (strcmp(by_field, "balance") == 0) sql = ACCOUNT_SELECT "ORDER BY a.balance DESC";
    else if (strcmp(by_field, "name")    == 0) sql = ACCOUNT_SELECT "ORDER BY u.name";
    else if (strcmp(by_field, "id")      == 0) sql = ACCOUNT_SELECT "ORDER BY a.account_id";
    else return ERR_INVALID_INPUT;

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(bank_db, sql, -1, &st, NULL) != SQLITE_OK) return ERR_DB;

    int rc = build_list(st, out_head);
    sqlite3_finalize(st);
    return rc;
}

int get_bank_summary(sqlite3 *bank_db, sqlite3_int64 *out_customers, sqlite3_int64 *out_accounts, double *out_total_balance)
{
    if (bank_db == NULL || out_customers == NULL || out_accounts == NULL || out_total_balance == NULL)
        return ERR_INVALID_INPUT;

    const char *sql =
        "SELECT (SELECT COUNT(*) FROM users), "
        "       (SELECT COUNT(*) FROM accounts), "
        "       (SELECT COALESCE(SUM(balance), 0) FROM accounts)";

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(bank_db, sql, -1, &st, NULL) != SQLITE_OK) return ERR_DB;

    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        *out_customers     = sqlite3_column_int64(st, 0);
        *out_accounts      = sqlite3_column_int64(st, 1);
        *out_total_balance = sqlite3_column_double(st, 2);
    }
    sqlite3_finalize(st);
    return (rc == SQLITE_ROW) ? OK : ERR_DB;
}