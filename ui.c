#include "ui.h"

#include "account.h"
#include "queue.h"
#include "transaction.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADMIN_PIN "0000" /* Replace before the final demonstration if the team changes it. */
#define INPUT_SIZE 128
#define HISTORY_SIZE 10
#define LIST_LIMIT 200

static int input_eof = 0;

enum { INPUT_EOF = -100 };

static int read_line(const char *prompt, char *buffer, size_t capacity)
{
    size_t length;

    if (buffer == NULL || capacity < 2)
        return ERR_INVALID_INPUT;

    printf("%s", prompt);
    fflush(stdout);

    if (fgets(buffer, (int)capacity, stdin) == NULL)
    {
        input_eof = 1;
        return INPUT_EOF;
    }

    length = strlen(buffer);
    if (length > 0 && buffer[length - 1] == '\n')
    {
        buffer[length - 1] = '\0';
    }
    else if (!feof(stdin))
    {
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF) { }
        printf("Input is too long. Please try again.\n");
        return ERR_INVALID_INPUT;
    }

    return OK;
}

static int read_integer(const char *prompt, long long minimum,
                        long long maximum, long long *out_value)
{
    char input[INPUT_SIZE];
    char *end = NULL;
    long long value;
    int rc;

    if (out_value == NULL)
        return ERR_INVALID_INPUT;

    rc = read_line(prompt, input, sizeof(input));
    if (rc != OK)
        return rc;

    errno = 0;
    value = strtoll(input, &end, 10);
    if (errno == ERANGE || end == input || *end != '\0' ||
        value < minimum || value > maximum)
    {
        printf("Invalid input. Enter a whole number from %lld to %lld.\n",
               minimum, maximum);
        return ERR_INVALID_INPUT;
    }

    *out_value = value;
    return OK;
}

static int read_id(const char *prompt, sqlite3_int64 *out_id)
{
    long long value;
    int rc = read_integer(prompt, 1, LLONG_MAX, &value);
    if (rc == OK && out_id != NULL)
        *out_id = (sqlite3_int64)value;
    else if (out_id == NULL)
        return ERR_INVALID_INPUT;
    return rc;
}

static int read_choice(const char *prompt, int minimum, int maximum,
                       int *out_choice)
{
    long long value;
    int rc = read_integer(prompt, minimum, maximum, &value);
    if (rc == OK && out_choice != NULL)
        *out_choice = (int)value;
    else if (out_choice == NULL)
        return ERR_INVALID_INPUT;
    return rc;
}

static int read_amount(const char *prompt, double *out_amount)
{
    char input[INPUT_SIZE];
    char *end = NULL;
    const char *dot;
    double value;
    int rc;

    if (out_amount == NULL)
        return ERR_INVALID_INPUT;

    rc = read_line(prompt, input, sizeof(input));
    if (rc != OK)
        return rc;

    if (input[0] == '\0' || input[0] == '-' || input[0] == '+')
        goto invalid;

    for (size_t i = 0; input[i] != '\0'; ++i)
        if (!isdigit((unsigned char)input[i]) && input[i] != '.')
            goto invalid;

    dot = strchr(input, '.');
    if (dot != NULL && (strchr(dot + 1, '.') != NULL ||
                        strlen(dot + 1) > 2 || dot == input || dot[1] == '\0'))
        goto invalid;

    errno = 0;
    value = strtod(input, &end);
    if (errno == ERANGE || end == input || *end != '\0' ||
        !isfinite(value) || value <= 0.0 || value > 1000000000000.0)
        goto invalid;

    *out_amount = value;
    return OK;

invalid:
    printf("Enter a positive amount with no more than two decimal places.\n");
    return ERR_INVALID_AMOUNT;
}

static int read_text(const char *prompt, char *buffer, size_t capacity)
{
    int rc = read_line(prompt, buffer, capacity);
    if (rc == OK && buffer[0] == '\0')
    {
        printf("This field cannot be empty.\n");
        return ERR_INVALID_INPUT;
    }
    return rc;
}

static void print_status_message(int status)
{
    switch (status)
    {
        case OK:                     printf("Operation completed successfully.\n"); break;
        case ERR_INVALID_INPUT:      printf("Invalid input.\n"); break;
        case ERR_NOT_FOUND:          printf("The requested record was not found.\n"); break;
        case ERR_AUTH_FAILED:        printf("Login failed. Check the account number and PIN.\n"); break;
        case ERR_DUPLICATE_TYPE:     printf("This customer already has that account type.\n"); break;
        case ERR_ACCOUNT_INACTIVE:   printf("This account is frozen or closed.\n"); break;
        case ERR_INSUFFICIENT_FUNDS: printf("Insufficient available balance.\n"); break;
        case ERR_LINK_DOWN:          printf("The simulated bank connection is unavailable.\n"); break;
        case ERR_DB:                 printf("A database operation failed.\n"); break;
        case ERR_MEMORY:             printf("Not enough memory to complete the operation.\n"); break;
        case ERR_INVALID_AMOUNT:     printf("The amount is invalid.\n"); break;
        case ERR_INVALID_ACCOUNT:    printf("The account number is invalid.\n"); break;
        case ERR_WRONG_ACCOUNT_TYPE: printf("This operation requires a savings account.\n"); break;
        default:                     printf("Operation failed (status %d).\n", status); break;
    }
}

static void print_account(const AccountNode *account)
{
    printf("Account: %lld | Owner: %s | Type: %s | Balance: %.2f | Status: %s\n",
           (long long)account->account_id, account->owner_name,
           account->account_type, account->balance, account->status);
}

static void print_account_list(const AccountNode *head)
{
    const AccountNode *node;
    if (head == NULL)
    {
        printf("No accounts found.\n");
        return;
    }
    for (node = head; node != NULL; node = node->next)
        print_account(node);
}

static int authenticate_admin(void)
{
    char pin[INPUT_SIZE];
    int rc = read_line("Administrator PIN: ", pin, sizeof(pin));
    if (rc != OK)
        return rc;
    if (strcmp(pin, ADMIN_PIN) != 0)
    {
        printf("Incorrect administrator PIN.\n");
        return ERR_AUTH_FAILED;
    }
    return OK;
}

static int perform_direct_deposit(sqlite3 *bank_db, sqlite3_int64 account_id,
                                  double amount)
{
    int rc;
    reset_link_check_sequence();
    rc = deposit(bank_db, account_id, amount, is_link_up);
    end_link_check_sequence();
    return rc;
}

static int perform_direct_withdrawal(sqlite3 *bank_db, sqlite3_int64 account_id,
                                     double amount)
{
    int rc;
    reset_link_check_sequence();
    rc = withdraw(bank_db, account_id, amount, is_link_up);
    end_link_check_sequence();
    return rc;
}

static int perform_direct_transfer(sqlite3 *bank_db, sqlite3_int64 sender_id,
                                   sqlite3_int64 receiver_id, double amount)
{
    int rc;
    reset_link_check_sequence();
    rc = transfer(bank_db, sender_id, receiver_id, amount, is_link_up);
    end_link_check_sequence();
    return rc;
}

static int refresh_customer_accounts(sqlite3 *bank_db, sqlite3_int64 user_id,
                                     AccountNode **accounts)
{
    free_account_list(accounts);
    return load_user_accounts(bank_db, user_id, accounts);
}

static int select_active_account(AccountNode *accounts, sqlite3_int64 *selected_id)
{
    AccountNode *node;
    int active_count = 0;
    long long choice;

    printf("\nYour accounts (only ACTIVE accounts can be selected):\n");
    for (node = accounts; node != NULL; node = node->next)
    {
        print_account(node);
        if (strcmp(node->status, "active") == 0)
            active_count++;
    }

    if (active_count == 0)
        return ERR_ACCOUNT_INACTIVE;

    if (active_count == 1)
    {
        for (node = accounts; node != NULL; node = node->next)
            if (strcmp(node->status, "active") == 0)
            {
                *selected_id = node->account_id;
                printf("Only one active account is available; selected account %lld.\n",
                       (long long)*selected_id);
                return OK;
            }
    }

    if (read_integer("Enter the account number you want to use: ",
                     1, LLONG_MAX, &choice) != OK)
        return input_eof ? INPUT_EOF : ERR_INVALID_INPUT;

    node = find_account_in_list(accounts, (sqlite3_int64)choice);
    if (node == NULL || strcmp(node->status, "active") != 0)
    {
        printf("That account is not active or does not belong to this customer.\n");
        return ERR_INVALID_ACCOUNT;
    }

    *selected_id = node->account_id;
    return OK;
}

static void show_atm_menu(void)
{
    printf("\n========== ATM MENU ==========\n"
           "1. Check balance\n"
           "2. Deposit\n"
           "3. Withdraw\n"
           "4. Transfer\n"
           "5. Mini-statement\n"
           "6. Open other account\n"
           "7. Switch account\n"
           "8. Logout\n"
           "==============================\n");
}

/* Restore the simulated gateway after a one-time fault and immediately
   attempt to replay all pending work. Persistent admin-offline mode is
   not changed by this helper. */
static void recover_and_sync_queue(sqlite3 *bank_db, sqlite3 *atm_db)
{
    size_t synced = 0, failed = 0;
    int rc;

    (void)set_link_status(1);
    rc = sync_pending_queue(bank_db, atm_db, &synced, &failed);
    printf("Automatic recovery: gateway ONLINE; %zu queued transaction(s) synchronized, %zu marked failed.\n",
           synced, failed);
    if (rc == ERR_LINK_DOWN)
        printf("Connection is still unavailable. Remaining transactions stay pending for the next retry.\n");
    else if (rc != OK)
        printf("Automatic synchronization could not finish (status %d). Pending items are retained.\n", rc);
}

static int atm_deposit(sqlite3 *bank_db, sqlite3 *atm_db,
                       sqlite3_int64 account_id)
{
    double amount;
    int rc = read_amount("Deposit amount: ", &amount);
    if (rc != OK) return rc;

    if (!is_link_up())
    {
        rc = queue_transaction(atm_db, account_id, "deposit", amount, 0, 0);
        if (rc == OK)
            printf("Connection is offline. Deposit saved locally as pending.\n");
        else
            print_status_message(rc);
        return rc;
    }

    rc = perform_direct_deposit(bank_db, account_id, amount);
    if (rc == ERR_LINK_DOWN)
    {
        (void)set_link_status(0);
        rc = queue_transaction(atm_db, account_id, "deposit", amount, 0, 0);
        if (rc == OK)
        {
            printf("Connection dropped during processing. Deposit was safely queued.\n");
            recover_and_sync_queue(bank_db, atm_db);
        }
        else print_status_message(rc);
        return rc;
    }

    if (rc == OK)
        printf("Deposit completed: %.2f\n", amount);
    else
        print_status_message(rc);
    return rc;
}

static int atm_withdraw(sqlite3 *bank_db, sqlite3_int64 account_id)
{
    double amount;
    int rc = read_amount("Withdrawal amount: ", &amount);
    if (rc != OK) return rc;

    if (!is_link_up())
    {
        printf("Withdrawal cannot be processed while the bank connection is offline. It was not queued.\n");
        return ERR_LINK_DOWN;
    }

    rc = perform_direct_withdrawal(bank_db, account_id, amount);
    if (rc == ERR_LINK_DOWN)
    {
        (void)set_link_status(1);
        printf("The one-time connection failure has ended. Gateway restored ONLINE; withdrawal was rolled back and not queued.\n");
    }
    if (rc == OK)
        printf("Withdrawal completed: %.2f\n", amount);
    else
        print_status_message(rc);
    return rc;
}

static int atm_transfer(sqlite3 *bank_db, sqlite3 *atm_db,
                        sqlite3_int64 sender_id)
{
    sqlite3_int64 receiver_id;
    double amount;
    int rc = read_id("Recipient account number: ", &receiver_id);
    if (rc != OK) return rc;
    if (receiver_id == sender_id)
    {
        printf("You cannot transfer to the same account.\n");
        return ERR_INVALID_ACCOUNT;
    }
    rc = read_amount("Transfer amount: ", &amount);
    if (rc != OK) return rc;

    if (!is_link_up())
    {
        rc = queue_transaction(atm_db, sender_id, "transfer", amount,
                               receiver_id, 1);
        if (rc == OK)
            printf("Connection is offline. Transfer saved locally as pending; recipient validation will occur during synchronization.\n");
        else
            print_status_message(rc);
        return rc;
    }

    rc = perform_direct_transfer(bank_db, sender_id, receiver_id, amount);
    if (rc == ERR_LINK_DOWN)
    {
        (void)set_link_status(0);
        rc = queue_transaction(atm_db, sender_id, "transfer", amount,
                               receiver_id, 1);
        if (rc == OK)
        {
            printf("Connection dropped during processing. Transfer was safely queued.\n");
            recover_and_sync_queue(bank_db, atm_db);
        }
        else print_status_message(rc);
        return rc;
    }

    if (rc == OK)
        printf("Transfer completed: %.2f\n", amount);
    else
        print_status_message(rc);
    return rc;
}

static int show_mini_statement(sqlite3 *bank_db, sqlite3_int64 account_id)
{
    TransactionRecord records[HISTORY_SIZE];
    size_t count = 0;
    int rc = get_transaction_history(bank_db, account_id, records,
                                     HISTORY_SIZE, &count);
    if (rc != OK)
    {
        print_status_message(rc);
        return rc;
    }

    printf("\n------ MINI STATEMENT ------\n");
    if (count == 0)
        printf("No completed transactions found.\n");
    for (size_t i = 0; i < count; ++i)
    {
        printf("%s | %-12s | %10.2f", records[i].timestamp,
               records[i].type, records[i].amount);
        if (records[i].has_related_account)
            printf(" | Related account: %lld",
                   (long long)records[i].related_account_id);
        printf("\n");
    }
    printf("----------------------------\n");
    return OK;
}

int ui_run_atm(sqlite3 *bank_db, sqlite3 *atm_db)
{
    sqlite3_int64 account_id, user_id;
    AccountNode *accounts = NULL;
    char pin[INPUT_SIZE];
    int rc, choice;

    if (bank_db == NULL || atm_db == NULL)
        return ERR_INVALID_INPUT;

    printf("\n========== CUSTOMER LOGIN ==========\n");
    rc = read_id("Account number: ", &account_id);
    if (rc != OK) return rc;
    rc = read_line("Four-digit PIN: ", pin, sizeof(pin));
    if (rc != OK) return rc;

    rc = authenticate_user(bank_db, account_id, pin, &user_id);
    if (rc != OK)
    {
        print_status_message(rc);
        return rc;
    }

    rc = load_user_accounts(bank_db, user_id, &accounts);
    if (rc != OK)
    {
        print_status_message(rc);
        return rc;
    }

    printf("\nLogin successful.\n");
    {
        AccountNode *logged_in = find_account_in_list(accounts, account_id);
        if (logged_in == NULL || strcmp(logged_in->status, "active") != 0)
        {
            free_account_list(&accounts);
            return ERR_ACCOUNT_INACTIVE;
        }
    }

    for (;;)
    {
        AccountNode *current;
        show_atm_menu();
        rc = read_choice("Select an option: ", 1, 8, &choice);
        if (rc == INPUT_EOF || input_eof) break;
        if (rc != OK) continue;

        if (choice == 8)
        {
            printf("Customer logged out.\n");
            break;
        }
        if (choice == 7)
        {
            rc = select_active_account(accounts, &account_id);
            if (rc == INPUT_EOF || input_eof) break;
            if (rc != OK) print_status_message(rc);
            continue;
        }
        if (choice == 6)
        {
            char type[INPUT_SIZE];
            sqlite3_int64 new_account_id;
            rc = read_text("New account type (savings/checking): ",
                           type, sizeof(type));
            if (rc == INPUT_EOF || input_eof) break;
            if (rc != OK) continue;
            rc = create_account(bank_db, user_id, type, &new_account_id);
            if (rc == OK)
            {
                printf("New %s account opened. Account number: %lld\n",
                       type, (long long)new_account_id);
                refresh_customer_accounts(bank_db, user_id, &accounts);
            }
            else print_status_message(rc);
            continue;
        }

        current = find_account_in_list(accounts, account_id);
        if (current == NULL || strcmp(current->status, "active") != 0)
        {
            printf("The selected account is no longer active. Please switch accounts or log out.\n");
            continue;
        }

        switch (choice)
        {
            case 1:
            {
                double balance;
                rc = get_balance(bank_db, account_id, &balance);
                if (rc == OK) printf("Available balance: %.2f\n", balance);
                else print_status_message(rc);
                break;
            }
            case 2: (void)atm_deposit(bank_db, atm_db, account_id); break;
            case 3: (void)atm_withdraw(bank_db, account_id); break;
            case 4: (void)atm_transfer(bank_db, atm_db, account_id); break;
            case 5: (void)show_mini_statement(bank_db, account_id); break;
            default: break;
        }
        if (input_eof) break;
    }

    free_account_list(&accounts);
    return OK;
}

static void show_bank_menu(void)
{
    printf("\n======= BANK ADMIN MENU =======\n"
           "1. Register customer and first account\n"
           "2. Open additional account\n"
           "3. Freeze active account\n"
           "4. Unfreeze frozen account\n"
           "5. Close account\n"
           "6. Reopen closed account (admin override)\n"
           "7. Search accounts\n"
           "8. Sort accounts\n"
           "9. Apply savings interest\n"
           "10. View bank summary\n"
           "11. View pending/failed queue\n"
           "12. Manually retry pending transactions\n"
           "13. Set gateway ONLINE/OFFLINE (restoring ONLINE auto-syncs)\n"
           "14. Simulate one-time failure (auto-recovers and retries)\n"
           "15. Logout\n"
           "===============================\n");
}

static int admin_register_customer(sqlite3 *bank_db)
{
    char name[INPUT_SIZE], pin[INPUT_SIZE], type[INPUT_SIZE];
    sqlite3_int64 user_id, account_id;
    int rc;
    if ((rc = read_text("Customer name: ", name, sizeof(name))) != OK) return rc;
    if (strlen(name) > 50) { printf("Name must be 50 characters or fewer.\n"); return ERR_INVALID_INPUT; }
    if ((rc = read_line("Four-digit customer PIN: ", pin, sizeof(pin))) != OK) return rc;
    if ((rc = read_text("First account type (savings/checking): ", type, sizeof(type))) != OK) return rc;
    rc = register_customer(bank_db, name, pin, type, &user_id, &account_id);
    if (rc == OK)
        printf("Customer registered. User ID: %lld | First account number: %lld\n",
               (long long)user_id, (long long)account_id);
    else print_status_message(rc);
    return rc;
}

static int admin_open_account(sqlite3 *bank_db)
{
    sqlite3_int64 user_id, account_id;
    char type[INPUT_SIZE];
    int rc = read_id("Customer user ID: ", &user_id);
    if (rc != OK) return rc;
    rc = read_text("Account type (savings/checking): ", type, sizeof(type));
    if (rc != OK) return rc;
    rc = create_account(bank_db, user_id, type, &account_id);
    if (rc == OK) printf("Account opened. New account number: %lld\n", (long long)account_id);
    else print_status_message(rc);
    return rc;
}

static int admin_change_account_status(sqlite3 *bank_db, int action)
{
    sqlite3_int64 account_id;
    int rc = read_id("Account number: ", &account_id);
    if (rc != OK) return rc;

    switch (action)
    {
        case 1: rc = freeze_account(bank_db, account_id); break;
        case 2: rc = unfreeze_account(bank_db, account_id); break;
        case 3: rc = close_account(bank_db, account_id); break;
        case 4: rc = reopen_account(bank_db, account_id); break;
        default: return ERR_INVALID_INPUT;
    }

    if (rc == OK)
    {
        const char *labels[] = {"", "frozen", "active (unfrozen)", "closed", "active (reopened)"};
        printf("Account %lld is now %s.\n", (long long)account_id, labels[action]);
    }
    else if (rc == ERR_NOT_FOUND)
        printf("Account not found, or its current status does not allow this operation.\n");
    else
        print_status_message(rc);
    return rc;
}

static int admin_list_accounts(sqlite3 *bank_db, int sort_mode)
{
    AccountNode *head = NULL;
    char query[INPUT_SIZE];
    const char *field = "balance";
    int rc;

    if (sort_mode)
    {
        int choice;
        rc = read_choice("Sort by: 1. Balance  2. Name  3. Account ID: ", 1, 3, &choice);
        if (rc != OK) return rc;
        field = choice == 1 ? "balance" : (choice == 2 ? "name" : "id");
        rc = sort_accounts(bank_db, field, &head);
    }
    else
    {
        rc = read_text("Search by account number or customer name: ", query, sizeof(query));
        if (rc != OK) return rc;
        rc = search_account(bank_db, query, &head);
    }

    if (rc == OK) print_account_list(head);
    else print_status_message(rc);
    free_account_list(&head);
    return rc;
}

static int admin_interest(sqlite3 *bank_db)
{
    sqlite3_int64 account_id;
    double rate;
    int rc = read_id("Savings account number: ", &account_id);
    if (rc != OK) return rc;
    rc = read_amount("Interest rate (percent): ", &rate);
    if (rc != OK) return rc;
    rc = apply_interest(bank_db, account_id, rate);
    if (rc == OK) printf("Interest applied successfully.\n");
    else print_status_message(rc);
    return rc;
}

static int admin_bank_summary(sqlite3 *bank_db)
{
    sqlite3_int64 customers, accounts;
    double total;
    int rc = get_bank_summary(bank_db, &customers, &accounts, &total);
    if (rc == OK)
    {
        printf("\n--------- BANK SUMMARY ---------\n"
               "Customers: %lld\nAccounts: %lld\nTotal account balance: %.2f\n"
               "-------------------------------\n",
               (long long)customers, (long long)accounts, total);
    }
    else print_status_message(rc);
    return rc;
}

static int admin_queue_status(sqlite3 *atm_db)
{
    QueueRecord records[LIST_LIMIT];
    size_t count = 0;
    int rc = get_queue_status(atm_db, records, LIST_LIMIT, &count);
    if (rc != OK) { print_status_message(rc); return rc; }
    printf("\n--------- OFFLINE QUEUE HISTORY ---------\n");
    if (count == 0) printf("Queue history is empty.\n");
    for (size_t i = 0; i < count; ++i)
    {
        printf("#%lld | %s | Account %lld | %.2f | %s",
               (long long)records[i].queue_id, records[i].type,
               (long long)records[i].account_id, records[i].amount,
               records[i].sync_status);
        if (records[i].has_related_account)
            printf(" | Related account %lld", (long long)records[i].related_account_id);
        printf(" | %s\n", records[i].queued_at);
    }
    if (count == LIST_LIMIT)
        printf("Showing the first %d records (oldest first).\n", LIST_LIMIT);
    printf("-----------------------------------------\n");
    return OK;
}

static int admin_sync_queue(sqlite3 *bank_db, sqlite3 *atm_db)
{
    size_t synced = 0, failed = 0;
    int rc;
    if (!is_link_up())
    {
        printf("Cannot synchronize while the simulated gateway is offline.\n");
        return ERR_LINK_DOWN;
    }
    rc = sync_pending_queue(bank_db, atm_db, &synced, &failed);
    printf("Synchronization result: %zu synchronized, %zu failed.\n", synced, failed);
    if (rc == ERR_LINK_DOWN)
        printf("The connection dropped. Remaining entries were left pending.\n");
    else if (rc != OK)
        print_status_message(rc);
    return rc;
}

static int admin_link_status(sqlite3 *bank_db, sqlite3 *atm_db)
{
    int choice;
    int rc = read_choice("1. Gateway online  2. Gateway offline: ", 1, 2, &choice);
    if (rc != OK) return rc;
    rc = set_link_status(choice == 1);
    if (rc == OK)
    {
        printf("Simulated gateway is now %s.\n", choice == 1 ? "ONLINE" : "OFFLINE");
        if (choice == 1)
            recover_and_sync_queue(bank_db, atm_db);
    }
    else print_status_message(rc);
    return rc;
}

int ui_run_bank(sqlite3 *bank_db, sqlite3 *atm_db)
{
    int rc, choice;
    if (bank_db == NULL || atm_db == NULL)
        return ERR_INVALID_INPUT;

    printf("\n========== BANK ADMINISTRATOR LOGIN ==========\n");
    rc = authenticate_admin();
    if (rc != OK) return rc;
    printf("Administrator login successful.\n");

    for (;;)
    {
        show_bank_menu();
        rc = read_choice("Select an option: ", 1, 15, &choice);
        if (rc == INPUT_EOF || input_eof) break;
        if (rc != OK) continue;
        if (choice == 15) { printf("Administrator logged out.\n"); break; }

        switch (choice)
        {
            case 1: (void)admin_register_customer(bank_db); break;
            case 2: (void)admin_open_account(bank_db); break;
            case 3: (void)admin_change_account_status(bank_db, 1); break;
            case 4: (void)admin_change_account_status(bank_db, 2); break;
            case 5: (void)admin_change_account_status(bank_db, 3); break;
            case 6: (void)admin_change_account_status(bank_db, 4); break;
            case 7: (void)admin_list_accounts(bank_db, 0); break;
            case 8: (void)admin_list_accounts(bank_db, 1); break;
            case 9: (void)admin_interest(bank_db); break;
            case 10: (void)admin_bank_summary(bank_db); break;
            case 11: (void)admin_queue_status(atm_db); break;
            case 12: (void)admin_sync_queue(bank_db, atm_db); break;
            case 13: (void)admin_link_status(bank_db, atm_db); break;
            case 14:
                rc = simulate_link_drop_during_next_transaction();
                if (rc == OK) printf("One-time failure armed. The next transaction will be interrupted; the gateway will recover and queued work will be retried automatically.\n");
                else print_status_message(rc);
                break;
            default: break;
        }
        if (input_eof) break;
    }
    return OK;
}
