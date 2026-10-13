# SecureTrace

Digital Evidence & Case Management System for Universiti Teknikal Malaysia Melaka (UTeM), Faculty of AI and Cyber Security (FTKKI).

SecureTrace is a **defensive record-keeping** application. It tracks cases, evidence, investigators, chain of custody, users, analysis queries, and reports. It does **not** include hacking, scanning, exploitation, or evidence-extraction tools.

```
SecureTrace  |  Digital Evidence & Case Management
UTeM         |  Faculty of AI and Cyber Security (FTKKI)
```

## Features

- **Cases** — create, list, read, update, soft-delete, search, restore (admin)
- **Evidence** — SHA-256 hashing from a file path or typed text, integrity verify, soft-delete, restore (admin)
- **Investigators** — full CRUDS (admin)
- **Custody transfers** — append-only Create / Read / Search (no silent edits)
- **Users** — PBKDF2-HMAC-SHA256 passwords, lockout after 3 failed logins, RBAC
- **Analysis & Calculations** — unary COUNT/MAX/MIN, grouping, joins, subqueries, decision support
- **Reports** — tables, summary lists, text bar charts, trends, audit log, TXT/CSV export
- **Backup** — admin export of all tables as CSV + SQL
- **Console UI** plus optional **Win32 GUI** (`securetrace_gui`)

### Roles

| Role | Access |
|------|--------|
| **admin** | Everything: users, audit, backup, restore, all modules |
| **investigator** | Cases, evidence, custody, analysis, reports |
| **viewer** | Read-only summary reports and analysis; description and storage location are masked |

## Tech stack

- C++17
- CMake 3.16+
- MySQL 8 / MariaDB 10.4+
- MySQL Connector/C++ (JDBC / classic API)
- OpenSSL (SHA-256, PBKDF2-HMAC-SHA256)

## Quick start

### 1. Database

As a MySQL administrator:

```sql
SOURCE sql/schema.sql;
SOURCE sql/create_app_user.sql;
SOURCE sql/seed.sql;
```

Copy the example config and match the app-user password:

```bash
cp config/config.example.ini config/config.ini
```

Default app user: `securetrace_app` / `ChangeMe_AppUser_1`

### 2. Demo accounts

| Username | Password     | Role         |
|----------|--------------|--------------|
| `admin`  | `Admin@123`  | admin        |
| `nurul`  | `Invest@123` | investigator |
| `viewer` | `Viewer@123` | viewer       |

### 3. Build

**Windows**

```bat
build.bat
```

Or:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
  -DMYSQL_CONNECTOR_DIR="C:\path\to\mysql-connector-c++" ^
  -DOPENSSL_ROOT_DIR="C:\Program Files\OpenSSL-Win64"
cmake --build build --config Release
```

Copy `mysqlcppconn*.dll` next to the `.exe`. Run from a folder that contains `config/`.

```bat
build\Release\securetrace.exe
build\Release\securetrace.exe --help
build\Release\securetrace.exe --demo
```

**Linux**

```bash
sudo apt install g++ cmake libssl-dev libmysqlcppconn-dev mysql-server
chmod +x build.sh
./build.sh
./build/securetrace
```

## Usage

```
securetrace              Start the console app
securetrace --help       Help screen
securetrace --demo       Reload schema.sql + seed.sql (needs MySQL root)
```

After login you get a **dashboard** (open cases, overdue cases, unverified evidence, last login), then the role-based main menu.

Navigation: option number, `0` or `b` to go back, `c` to cancel a form field, `y`/`n` to confirm delete or close.

## Project structure

```
SecureTrace/
├── CMakeLists.txt
├── build.bat / build.sh
├── config/
│   └── config.example.ini      # copy to config.ini (not committed)
├── include/                    # headers
├── src/                        # implementation
├── sql/
│   ├── schema.sql
│   ├── seed.sql
│   ├── create_app_user.sql     # least-privilege GRANTs
│   └── alter_phase2.sql
├── tests/
│   ├── security_tests.cpp
│   └── feature_tests.cpp
├── docs/README.md              # ER diagram, data dictionary, mapping
└── reports/                    # TXT/CSV exports and backups (runtime)
```

## Security

- Prepared statements for every query
- PBKDF2-HMAC-SHA256, 100000+ iterations, per-user random salt
- Password rule: 8+ characters, at least one digit and one symbol
- Account lock for 5 minutes after 3 failed logins
- RBAC checked in the backend before each action
- Auto-logout after 10 minutes idle
- Audit log for login, logout, create/update/delete, hash verify, exports
- App connects as `securetrace_app`, not root
- Custody table is INSERT/SELECT only (no UPDATE/DELETE grant)

## Tests

From a working directory that can load `config/config.ini`:

```bash
# Windows
build\Release\securetrace_tests.exe
build\Release\securetrace_feature_tests.exe

# Linux
./build/securetrace_tests
./build/securetrace_feature_tests
```

Security tests cover SQL injection rejection, lockout, RBAC denial, and SHA-256 mismatch detection.

## Documentation

| Document | What it is |
|----------|------------|
| **[docs/USER_MANUAL.md](docs/USER_MANUAL.md)** | Operator handbook — how to use every menu and feature |
| **[docs/HOW_IT_WORKS.md](docs/HOW_IT_WORKS.md)** | Technical guide — architecture, database, security, SQL, modules A to Z |
| **[docs/README.md](docs/README.md)** | Setup extras: ER diagram, data dictionary, test table, requirements mapping |

## Academic use

Built for a UTeM FTKKI workshop. Keep credentials out of git (`config/config.ini` is gitignored). Change the demo passwords before any shared or production use.

## License

Course / workshop project. See repository owner for reuse terms.
