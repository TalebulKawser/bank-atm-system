#include "db.h"
#include "status.h"
#include "ui.h"
#include "queue.h"

#include <stdio.h>

int main(void)
{
    sqlite3 *bank_db = NULL;
    sqlite3 *atm_db = NULL;
    int result;
    int choice;

    printf("========================================\n");
    printf("       BANK MANAGEMENT SYSTEM           \n");
    printf("========================================\n");

    result = db_open(&bank_db, &atm_db);
    if (result != 0)
    {
        fprintf(stderr, "Unable to open the application databases.\n");
        return 1;
    }

    result = db_init(bank_db, atm_db);
    if (result != 0)
    {
        fprintf(stderr, "Unable to initialize the application databases.\n");
        db_close(&bank_db, &atm_db);
        return 1;
    }

    /* On restart, attempt recovery of any work left pending by a prior run. */
    {
        size_t synced = 0, failed = 0;
        result = sync_pending_queue(bank_db, atm_db, &synced, &failed);
        if (synced > 0 || failed > 0)
            printf("Startup recovery: %zu transaction(s) synchronized, %zu marked failed.\n",
                   synced, failed);
        if (result != OK && result != ERR_LINK_DOWN)
            printf("Startup queue recovery was incomplete (status %d).\n", result);
    }

    for (;;)
    {
        printf("\n============= MAIN MENU =============\n");
        printf("1. ATM / Customer mode\n");
        printf("2. Bank administrator mode\n");
        printf("3. Exit\n");
        printf("=====================================\n");
        printf("Select an option: ");
        fflush(stdout);

        if (scanf("%d", &choice) != 1)
        {
            int ch;
            while ((ch = getchar()) != '\n' && ch != EOF) { }
            if (feof(stdin)) break;
            printf("Please enter 1, 2, or 3.\n");
            continue;
        }
        while (getchar() != '\n' && !feof(stdin)) { }

        if (choice == 1)
        {
            result = ui_run_atm(bank_db, atm_db);
            if (result != OK && result != ERR_AUTH_FAILED && result != ERR_INVALID_INPUT)
                printf("ATM mode ended with status %d.\n", result);
        }
        else if (choice == 2)
        {
            result = ui_run_bank(bank_db, atm_db);
            if (result != OK && result != ERR_AUTH_FAILED && result != ERR_INVALID_INPUT)
                printf("Bank mode ended with status %d.\n", result);
        }
        else if (choice == 3)
        {
            break;
        }
        else
        {
            printf("Invalid option. Choose 1, 2, or 3.\n");
        }
    }

    if (db_close(&bank_db, &atm_db) != 0)
    {
        fprintf(stderr, "Warning: one or more database connections could not be closed.\n");
        return 1;
    }

    printf("Application closed. Goodbye.\n");
    return 0;
}
