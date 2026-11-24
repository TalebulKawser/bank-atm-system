# Bank Management System with ATM Simulation

A console-based bank and ATM simulation written in C using SQLite. The program maintains two SQLite databases:

- `bank.db` stores users, accounts, and completed transactions.
- `atm.db` stores offline deposit/transfer queue history.

## Requirements

- GCC or another C11-compatible compiler
- The included SQLite amalgamation (`sqlite3.c` and `sqlite3.h`)
- A terminal/console

## Build

From the project directory:

```bash
gcc -Wall -Wextra -std=c11 main.c ui.c account.c transaction.c queue.c db.c sqlite3.c -o bank_atm -lm
```

On Windows PowerShell, run the resulting executable with:

```powershell
.\bank_atm.exe
```

On Linux/macOS:

```bash
./bank_atm
```

The two database files and required tables are created automatically on first launch.

## Initial administrator PIN

The current demonstration PIN is `0000`, defined by `ADMIN_PIN` in `ui.c`. Change it before a public demonstration if the team agrees on another value.

## Application functions

### ATM/customer mode

- Login using account number and four-digit PIN
- View balance and the ten most recent completed transactions
- Deposit, withdraw, and transfer
- Queue deposits and transfers while the simulated gateway is offline
- Reject withdrawals while offline (withdrawals are never queued)
- Open a customer's other account type and switch between active accounts. The switch list displays all accounts and their statuses, but frozen/closed accounts cannot be selected.

### Bank administrator mode

- Register customers and open additional accounts
- Freeze, unfreeze, close, and administratively reopen accounts
- Search accounts by account number or customer name
- Sort accounts by balance, name, or account number
- Apply interest to savings accounts
- View bank summary and offline queue history
- View pending/synced/failed queue history and manually request synchronization
- Set the simulated gateway online/offline (persists until changed or the app restarts) and arm a one-time failure for the next transaction only
- Automatically retry a one-time interrupted transaction after recovery; automatically synchronize pending work when the gateway is restored to ONLINE and at application startup

## Offline transaction rules

- Customer login still checks the bank database.
- Only deposits and transfers can be queued while offline.
- Withdrawals are rejected while offline and are never queued.
- Pending queue entries are replayed oldest first. The application attempts synchronization at startup, when an administrator restores the gateway to ONLINE, and immediately after a one-time failure is recovered. The administrator can also request a manual synchronization at any time.
- A one-time simulated gateway failure is retried once automatically for the same queue item after the simulated connection recovers. A persistent OFFLINE setting leaves pending work queued until the gateway is restored.
- If synchronization encounters a persistent outage or database error, unprocessed entries remain pending for a later retry. Business-rule failures (such as insufficient funds or an invalid recipient) are marked failed and retained in queue history.
- Queue records are retained as history and marked `pending`, `synced`, or `failed`.
- Only active accounts can be frozen; frozen accounts can be unfrozen. Active or frozen accounts can be closed. A closed account can be reopened only through the administrator menu (an explicit academic-simulation override).
- Pending transactions do not appear in the customer's completed transaction history until synchronized.

## Project scope

Automated test programs are intentionally kept outside this application repository. They are not compiled or run by the application. The application build includes only the production source files shown above. Runtime databases (`bank.db` and `atm.db`) are local application data and are ignored by Git.

## Important scope note

This is an academic simulation, not production banking software. The administrator PIN is fixed in the UI, customer PINs are stored as plain text, the gateway is simulated in process memory, and monetary values use SQLite `REAL` as specified by the project design. Do not use real customer credentials or money.
