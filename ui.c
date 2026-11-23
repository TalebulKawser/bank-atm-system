#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * TEMPORARY DEVELOPMENT PIN.
 * The team must agree on the final administrator PIN and replace this
 * value before the final demonstration.
 */
#define ADMIN_PIN "0000"
#define INPUT_SIZE 128

static int read_line(const char *prompt, char *buffer, size_t capacity)
{
    size_t length;

    if (buffer == NULL || capacity < 2)
        return ERR_INVALID_INPUT;

    printf("%s", prompt);

    if (fgets(buffer, (int)capacity, stdin) == NULL)
        return ERR_INVALID_INPUT;

    length = strlen(buffer);

    if (length > 0 && buffer[length - 1] == '\n')
        buffer[length - 1] = '\0';
    else
    {
        int character;
        while ((character = getchar()) != '\n' && character != EOF)
        {
            /* Discard the remainder of an overlong input line. */
        }
    }

    return OK;
}

static int read_menu_choice(const char *prompt, int minimum, int maximum,
                            int *out_choice)
{
    char input[INPUT_SIZE];
    char *end_pointer;
    long value;

    if (out_choice == NULL)
        return ERR_INVALID_INPUT;

    if (read_line(prompt, input, sizeof(input)) != OK)
        return ERR_INVALID_INPUT;

    value = strtol(input, &end_pointer, 10);

    if (input[0] == '\0' || *end_pointer != '\0' ||
        value < minimum || value > maximum)
    {
        printf("Invalid choice. Enter a number from %d to %d.\n",
               minimum, maximum);
        return ERR_INVALID_INPUT;
    }

    *out_choice = (int)value;
    return OK;
}

static void show_atm_menu(void)
{
    printf("\n========== ATM MENU ==========\n");
    printf("1. Check balance\n");
    printf("2. Deposit\n");
    printf("3. Withdraw\n");
    printf("4. Transfer\n");
    printf("5. Mini-statement\n");
    printf("6. Open other account\n");
    printf("7. Logout\n");
    printf("==============================\n");
}

static int authenticate_admin(void)
{
    char entered_pin[INPUT_SIZE];

    if (read_line("Enter administrator PIN: ",
                  entered_pin, sizeof(entered_pin)) != OK)
        return 0;

    if (strcmp(entered_pin, ADMIN_PIN) != 0)
    {
        printf("Incorrect administrator PIN.\n");
        return 0;
    }

    return 1;
}

static void show_bank_menu(void)
{
    printf("\n======= BANK ADMIN MENU =======\n");
    printf("1. Register customer and first account\n");
    printf("2. Open additional account\n");
    printf("3. Freeze account\n");
    printf("4. Close account\n");
    printf("5. Search accounts\n");
    printf("6. Sort accounts\n");
    printf("7. Apply savings interest\n");
    printf("8. View bank summary\n");
    printf("9. View pending/failed queue\n");
    printf("10. Synchronize pending transactions\n");
    printf("11. Change simulated link status\n");
    printf("12. Logout\n");
    printf("===============================\n");
}

int ui_run_atm(sqlite3 *bank_db, sqlite3 *atm_db)
{
    int choice;

    if (bank_db == NULL || atm_db == NULL)
        return ERR_INVALID_INPUT;

    printf("\nATM mode selected.\n");
    printf("Customer login will be connected during module integration.\n");

    for (;;)
    {
        show_atm_menu();

        if (read_menu_choice("Select an option: ", 1, 7, &choice) != OK)
            continue;

        if (choice == 7)
        {
            printf("Leaving ATM mode.\n");
            return OK;
        }

        printf("This ATM operation is not connected yet.\n");
    }
}

int ui_run_bank(sqlite3 *bank_db, sqlite3 *atm_db)
{
    int choice;

    if (bank_db == NULL || atm_db == NULL)
        return ERR_INVALID_INPUT;

    printf("\nBank administrator mode selected.\n");

    if (!authenticate_admin())
        return ERR_AUTH_FAILED;

    printf("Administrator login successful.\n");

    for (;;)
    {
        show_bank_menu();

        if (read_menu_choice("Select an option: ", 1, 12, &choice) != OK)
            continue;

        if (choice == 12)
        {
            printf("Leaving bank mode.\n");
            return OK;
        }

        printf("This administrator operation is not connected yet.\n");
    }
}
