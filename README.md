# Stockroom • Inventory & Stock Management

A single-site inventory application with a C11 backend, SQLite database, responsive browser interface and interactive terminal client. Purchases, sales, stock balances, movement history and receipts share the same database. Python is used only by the regression tests.

Open **Preview.html** in a browser for a read-only sample dashboard. Follow the setup below to enter your own inventory and record transactions.

## Start on a Mac, Windows or Linux computer

Install Docker Desktop (or Docker Engine with Compose). Extract this ZIP, open a terminal in the directory containing compose.yaml, then run:

```sh
docker compose build
docker compose run --rm --user www-data inventory /usr/lib/cgi-bin/inventory init admin
docker compose up -d
```

Choose a password of 12–128 characters at the prompt. The init command runs once. Open **http://localhost:8080** and sign in. Data persists in the named volume when the container restarts. To stop the application, run `docker compose down`. Adding `-v` deletes the database volume.

## First transaction

1. Open **Products → Add product**. Enter a permanent product ID, supplier, opening quantity, unit cost, selling price and reorder settings.
2. Open **Record transaction**. Choose **Purchase** to receive stock or **Sale** to sell stock. Enter the quantity and supplier reference or customer name.
3. A sale opens its printable receipt. Reopen it from **Receipts**. The dashboard and movement ledger reflect committed transactions immediately.
4. Use **Reports** for 30-day demand-based reorder points, slow-moving stock, a what-if calculation and administrator-only financial totals and ledger reconciliation. Monetary values are INR. Times are UTC.
5. Administrators can add staff in **Team**. Both roles can maintain the catalogue and record purchases and sales. Administrators can also archive zero-stock items. Product IDs remain reserved for their entire history.

## Operations

Deployment target: one host with a persistent local SQLite filesystem, one Apache CGI service and modest internal traffic. The default port binds to loopback. For network access, terminate HTTPS at a reverse proxy, restrict access to intended staff, set COOKIE_SECURE=1 and recreate the container. Monitor available disk space and errors with `docker compose logs inventory`. Keep access to the host and database volume limited to the operator.

### Terminal

```sh
docker compose exec --user www-data inventory /usr/lib/cgi-bin/inventory
```

Sign in with the same account. The terminal supports product management, search, purchases, sales, receipts, movement history and stock summaries. Changes are committed per operation. Use the browser for printing receipts, planning reports and team management.

### Back up and restore

Create a consistent snapshot of the entire database and copy it off the application host. The snapshot contains business data and password hashes. Protect the copy. Use a new filename for each snapshot.

```sh
docker compose exec --user www-data inventory /usr/lib/cgi-bin/inventory backup /data/stock-001.db
docker compose cp inventory:/data/stock-001.db ./stock-001.db
```

To restore a snapshot **already in /data**, stop all writers (including terminal clients) and take a pre-restore backup with a fresh filename. Restore replaces current data and invalidates web sessions. Use an isolated copy to practice the procedure before live use.

```sh
docker compose stop inventory
docker compose run --rm --user www-data inventory /usr/lib/cgi-bin/inventory backup /data/before-restore.db
docker compose run --rm --user www-data inventory /usr/lib/cgi-bin/inventory restore /data/stock-001.db
docker compose up -d
```

For disaster recovery from an off-host copy, initialize a fresh volume, mount the snapshot read-only into the operator container (as in the import example below), then restore its mounted path. Verify the restored dashboard, a known receipt and ledger reconciliation. To reset an account password, run `docker compose exec --user www-data inventory /usr/lib/cgi-bin/inventory reset USERNAME` and enter a new password.

### Import the classroom project's data

Initialize the new database first. Before adding products or team members, put a copy of the old .dat files together in a directory called legacy next to compose.yaml. Make the files readable to the container's www-data user. From that directory's parent, run:

```sh
docker compose run --rm --user www-data -v "$PWD/legacy:/legacy:ro" inventory /usr/lib/cgi-bin/inventory import /legacy
```

The importer reads INV2, MOV1, SALE1 and legacy users.dat. It accepts an empty last-sale field, preserves historical invoice IDs, verifies that movement and sale quantities reconcile, and rolls back the entire import on inconsistent data. Unsupported older formats require a separate conversion. Retain historical invoice text files separately, especially if products were renamed. Imported receipt names use the catalogue name present at migration. Opening inventory value uses the legacy stored unit cost. Historical sub-paise amounts round to the nearest paise. Existing plaintext passwords are discarded. Set a fresh password for each imported account using the reset command. An existing bootstrap administrator with the same name keeps the newly chosen password and must match the imported role.

## Implementation

- src/main.c handles CGI requests, sessions, CSRF validation and operator commands.
- src/store.c owns schema creation, transactions, integer-paise weighted-average costing, password hashing and snapshot validation.
- src/views.c renders escaped HTML. public/ contains local CSS and optional JavaScript for the transaction form and printing.
- src/legacy.c validates and imports the old flat-file format in one transaction.
- src/terminal.c runs authenticated commands against the same database.
- tests/test_inventory.py exercises both interfaces against disposable databases.

A sale commits the receipt, quantity decrement, cost allocation and movement together under a SQLite write lock. Duplicate submission of the same form returns its original result. A sale allocates its share of the recorded stock value, rounded to the nearest paise, and selling the final unit clears the residual value. Receipts retain their sale-time name and price. Database backups omit active sessions. Login uses salted PBKDF2-HMAC-SHA256 and hidden password input. Five failed attempts for a known account trigger a five-minute lock. Web sessions expire after eight hours.

## Native build and tests

On a POSIX host install a C compiler, Make, SQLite development headers/libraries (SQLite 3.38+) and OpenSSL development headers/libraries. On Debian these packages are build-essential, libsqlite3-dev and libssl-dev. The tests additionally require Python 3. Native commands:

```sh
make
make test
export INVENTORY_DB="$PWD/inventory.sqlite"
./inventory init admin
./inventory
```

For native web hosting, install the executable as an Apache CGI program, serve public/ at the document root and configure INVENTORY_DB to a writable path outside that root. The supplied Docker configuration sets this up. The terminal and web service must use the same database path and appropriate filesystem permissions.
