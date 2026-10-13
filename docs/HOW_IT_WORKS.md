# How SecureTrace Works (A to Z)

This document explains the whole system: purpose, architecture, database, security, every module, SQL calculations, reports, GUI, tests, and runtime files. For click-by-click operator steps, see [USER_MANUAL.md](USER_MANUAL.md).

---

## 1. Purpose

SecureTrace is a **defensive digital-evidence case register**. Lecturers and students record:

- who owns a case
- what items were booked in
- cryptographic hashes of those items
- who held an item over time
- who logged in and what they changed

It is **not** a forensic acquisition toolkit. There is no disk imaging, memory dump, packet capture, vulnerability scan, or exploit code. Hashing only digests a file path or a string the operator already has.

It was built as a C++17 console (and optional Win32 GUI) workshop system for UTeM FAIX, against a MySQL/MariaDB schema in third normal form.

---

## 2. High-level architecture

```
Operator
   │
   ├─ console  src/main.cpp  + ConsoleUI
   └─ GUI      src/gui_main.cpp + GuiApp  (Windows)
              │
              ├─ Users / Cases / Evidence / Investigators
              ├─ CustodyTransfers / Analysis / Reports / Backup
              │
              ├─ AuditLog          → INSERT audit_log
              ├─ HashUtil          → OpenSSL SHA-256
              ├─ Users hashing     → OpenSSL PBKDF2-HMAC-SHA256
              └─ DatabaseConnection → MySQL Connector/C++ PreparedStatement
                                      │
                                      ▼
                               MariaDB / MySQL
                    investigators, users, cases, evidence,
                    custody_transfers, audit_log
                    (+ triggers on evidence/cases)
```

Rules baked into this layout:

- Modules never concatenate SQL strings with user text.
- Every query goes through `DatabaseConnection::prepare`.
- RBAC is applied twice: the menu hides illegal options, then each module `deny()` / `hasPermission()` checks again.
- Technical errors are written to `logs/app.log`; the operator sees a short “Operation failed” line.

---

## 3. Program startup (console)

`src/main.cpp` is the process entry.

1. Optional `--help` / `-h` / `?` prints Help and exits.
2. Creates `logs/`, `reports/`, `config/` if missing.
3. Opens `logs/app.log`.
4. Loads `config.ini` via `Config::findPath()` (`config/config.ini`, then a few relative fallbacks, then the example file). Missing config → friendly message, exit 1.
5. Optional `--demo` runs `sql/schema.sql` then `sql/seed.sql` through the `mysql` client as **root** (after `y` confirm).
6. `DatabaseConnection` connects with the **app** user from the ini file, not root. Unreachable DB → friendly message, exit 1.
7. Constructs AuditLog, Users, Cases, Evidence, Investigators, CustodyTransfers, Reports, Analysis, Backup.
8. Loop:
   - if the session is idle longer than `session_timeout_minutes` (default 10) → logout
   - if not logged in → Welcome (Login / Help / About / Exit)
   - if just logged in → Dashboard, then role-based Main Menu

The GUI (`WinMain` in `src/gui_main.cpp`) does the same connect/login path, then a Win32 window instead of menus.

---

## 4. Configuration

`config/config.example.ini` is committed. `config/config.ini` is gitignored.

```ini
[database]
host=127.0.0.1
port=3306
name=securetrace
user=securetrace_app
password=ChangeMe_AppUser_1

[security]
pbkdf2_iterations=100000
session_timeout_minutes=10
lockout_minutes=5
max_failed_attempts=3
```

`pbkdf2_iterations` cannot go below 100000 in `Config.cpp`. Database credentials are never hard-coded in C++.

---

## 5. Database design

### 5.1 Engine and normalisation

InnoDB, utf8mb4. Third normal form:

| Table | Independent facts | Depends on |
|-------|-------------------|------------|
| `investigators` | people who can be assigned | — |
| `users` | login accounts | — (not the same as investigators) |
| `cases` | one investigation | `assigned_investigator_id` → investigators |
| `evidence` | one bagged item | `case_id` → cases |
| `custody_transfers` | one hand-off | `evidence_id` → evidence |
| `audit_log` | one security event | `user_id` → users (nullable) |

Users and investigators are **separate** on purpose: a login (nurul) is not automatically the same row as “Puan Nurul Izzah” in investigators.

### 5.2 Keys, checks, indexes

Every table has an integer primary key. Foreign keys:

- cases → investigators (`ON DELETE RESTRICT`) so you cannot delete an investigator who still has cases
- evidence → cases (`RESTRICT`)
- custody → evidence (`RESTRICT`)
- audit_log → users (`ON DELETE SET NULL`) so deleting a user does not wipe history

CHECK constraints encode allowed enums (case type/priority/status, evidence type, user role), date order (`deadline >= opened_date`), “closed ⇒ closed_date set”, hash hex format, `size_mb > 0`.

Indexes exist on status, type, priority, dates, hashes, `is_active`, custody datetime, audit timestamp/action so list/search/report queries stay cheap on the seed set (15 cases, 40 evidence, 60 transfers).

### 5.3 Soft delete vs append-only vs hard delete

| Data | Delete behaviour |
|------|------------------|
| Cases, evidence | Soft: `is_active = 0`. Lists hide them; search can still find them. Admin restore sets `is_active = 1`. |
| Users | Soft: `is_active = 0`. Cannot log in. |
| Investigators | Hard `DELETE`, blocked if any case still points at them. |
| Custody | **No update, no delete.** Application menu omits those actions. MySQL user `securetrace_app` has only `SELECT, INSERT` on `custody_transfers`. |
| Audit | Insert only for the app user (`SELECT, INSERT`). |

### 5.4 Least-privilege MySQL user

`sql/create_app_user.sql` creates `securetrace_app`@`localhost` and grants:

- investigators, cases, evidence, users: SELECT, INSERT, UPDATE, DELETE
- custody_transfers, audit_log: SELECT, INSERT

The C++ app never connects as root. `--demo` is the only path that shells out to root, and only to reload workshop data.

### 5.5 Triggers

Defined in `sql/schema.sql`:

- **trg_evidence_soft_delete** — AFTER UPDATE on evidence, if `is_active` goes 1→0, insert `EVIDENCE_SOFT_DELETE`
- **trg_evidence_hard_delete** — AFTER DELETE, insert `EVIDENCE_HARD_DELETE`
- **trg_case_status_change** — AFTER UPDATE on cases, if `status` changed, insert `CASE_STATUS_CHANGE`

They read session variable `@app_user_id`, which `DatabaseConnection::setAppUserId` sets after login (`SET @app_user_id = ?`) so trigger rows can attribute the operator. Logout clears it.

### 5.6 Seed data

`sql/seed.sql` loads 5 investigators, 15 cases across months and types, 40 evidence items with precomputed SHA-256, 60 custody rows, 3 users. Password hashes were generated with PBKDF2-HMAC-SHA256, 100000 iterations, 16-byte random salts (hex in `password_hash` / `salt`).

---

## 6. Connection and SQL execution

`DatabaseConnection` (one instance, passed by reference):

- Uses Connector/C++ JDBC API (`sql::mysql::get_mysql_driver_instance`, `prepareStatement`)
- `prepare(sql)` always returns a `PreparedStatement`
- Placeholders `?` are bound with `setInt` / `setString` / `setDouble` / `setNull`
- Failures throw after logging `ex.what()`; UI shows a generic error

Search filters that look dynamic still use a **fixed** SQL string with `?` and `LIKE CONCAT('%', ?, '%')`. The application never builds `WHERE title = '" + userInput + "'"`.

`--demo` is the exception: it runs whole `.sql` files via the mysql CLI as root, not through Connector/C++.

---

## 7. Session, RBAC, passwords

### 7.1 Session object

`Session` in `include/Session.h`: `userId`, `username`, `role`, `authenticated`, `lastActivity`. `touch()` on each menu action. `timedOut(minutes)` uses `std::chrono::steady_clock`.

### 7.2 Login algorithm (`Users::authenticate`)

1. `SELECT ... FROM users WHERE username = ?` including `(locked_until > NOW()) AS is_locked`
2. Unknown user → FAIL audit, generic error (no user-enumeration message)
3. `is_active = 0` → disabled
4. Locked → refuse even if the password is right
5. PBKDF2 the typed password with that row’s salt; constant-time compare to `password_hash`
6. Mismatch → increment `failed_attempts`; if attempts ≥ 3, set `locked_until = NOW() + lockout_minutes`
7. Match → zero attempts, clear lock, `setAppUserId`, fill Session, SUCCESS audit

### 7.3 Password storage

- 16 random bytes from `RAND_bytes` → 32 hex chars (`salt`)
- `PKCS5_PBKDF2_HMAC(..., EVP_sha256(), iterations, 32 bytes)` → 64 hex chars (`password_hash`)
- Policy: length ≥ 8, at least one digit, at least one non-alnum symbol
- Console password entry uses `_getch` (Windows) or termios (Linux) and prints `*`

### 7.4 RBAC

`Users::hasPermission(session, feature)`:

| Feature | admin | investigator | viewer |
|---------|-------|--------------|--------|
| cases, evidence, custody, reports | yes | yes | no |
| analysis | yes | yes | via `reports_summary` on the analysis menu |
| users, backup, investigators, audit | yes | no | no |
| reports_summary | yes | yes | yes |

Each module still calls `deny()` so a crafted GUI click cannot skip the check. Viewer masking replaces description and `storage_location` with `********` in analysis/report/evidence list paths.

---

## 8. How each module works

### 8.1 Cases (`src/Cases.cpp`)

CRUDS against `cases`. Create always inserts `status = 'Open'`, `closed_date = NULL`, `is_active = 1`.

Closing:

1. Count `evidence` rows for that case with `is_active = 1 AND verified = 0`
2. If count > 0 and role ≠ admin → refuse, audit FAIL
3. If admin confirms override → `CASE_CLOSE_OVERRIDE` SUCCESS, then UPDATE status/closed_date
4. Trigger `trg_case_status_change` also writes `CASE_STATUS_CHANGE`

Deadline vs opened_date is enforced in C++ **and** by `chk_cases_dates`.

List/search: optional filters bound as `?`; ORDER BY is chosen from a whitelist of column expressions, not free user SQL.

### 8.2 Evidence (`src/Evidence.cpp` + `HashUtil`)

Create:

1. Confirm case is active
2. Hash source:
   - file: stream in 8 KiB chunks, `EVP_DigestUpdate` SHA-256, size = bytes / 1024² (min 0.01)
   - text: digest the UTF-8 bytes of the string
3. INSERT hash, size, metadata, `verified = 0`

Verify: hash again, compare hex strings, UPDATE `verified`, audit `HASH_VERIFY`.

Soft delete UPDATE `is_active = 0` (trigger logs it). Restore is the reverse, admin only.

### 8.3 Investigators

Straightforward INSERT/SELECT/UPDATE/DELETE. Delete first `COUNT(*)` cases with that `assigned_investigator_id`; if > 0, refuse. FK would also reject the DELETE.

### 8.4 Custody

Only INSERT/SELECT. Before INSERT, `SELECT is_active FROM evidence WHERE evidence_id = ?`. Inactive or missing → refuse.

JOIN reports/analysis attach `audit_log` rows whose `action = 'CREATE'` and `target = CONCAT('custody evidence_id=', id)` to show which **user account** recorded the transfer. Custody itself stores person names as text (`from_person` / `to_person`), not user IDs, because chain-of-custody often names locker staff who have no login.

### 8.5 Users (admin screens)

Create generates salt+hash. Disable sets `is_active = 0`. Unlock zeros attempts. An admin cannot disable or change the role of **their own** row (avoids lock-out of the last admin).

### 8.6 Analysis (`src/Analysis.cpp`)

Each menu leaf is a **named** feature with a `// REQUIREMENT: ...` comment and a one-line English explanation before the table.

| Group | SQL idea |
|-------|----------|
| UNARY COUNT/MAX/MIN | Scalar subqueries, one aggregate on one column |
| GROUP BY COUNT/SUM/AVG | `GROUP BY type/priority/month/case` |
| HAVING | `HAVING COUNT(open cases) >= 3` |
| JOIN | `cases JOIN investigators LEFT JOIN evidence`; `evidence JOIN custody LEFT JOIN audit_log LEFT JOIN users` |
| SUBQUERY | `NOT IN (SELECT ...)`; average load subquery inside HAVING; `deadline < (SELECT AVG(deadline) ...)` |
| Decision workload | `SUM(CASE priority WHEN 'Low' THEN 1 ... Critical 5)` ordered ASC → first row is the recommendation |
| Integrity % | `SUM(verified=1) * 100.0 / COUNT(*)` |
| Days remaining | `DATEDIFF(deadline, CURDATE())`; if negative, store `-ABS(...)` so overdue is visibly negative |

Viewer still runs these SELECTs but masked columns are blanked in C++ after the result set arrives.

### 8.7 Reports (`src/Reports.cpp`)

Builds a `ReportData` (title, slug, notes, sections of tables or chart lines). Console prints it, then **previews again** before export.

Date filter binds `opened_date` / `closed_date` / `collected_date` / `transfer_datetime` / `audit.timestamp` BETWEEN `from` AND `to`.

Charts: find max count, draw `#` proportional to `value/max * width`, print the numeric value.

Export writes `reports/<slug>_YYYYMMDD_HHMMSS.txt` (ASCII tables) and `.csv` (sectioned). `REPORT_EXPORT` audit includes the path.

Viewer menu is a shorter list (`showSummary`) so reports 2, 3, 6 never appear.

### 8.8 Backup (`src/Backup.cpp`)

`exportDatabase` loops a hard-coded list of tables and columns, `SELECT` each, writes CSV and quoted `INSERT` SQL under `reports/backups/backup_<stamp>/`. Not a binary InnoDB backup; it is a portable workshop dump. `BACKUP_EXPORT` is audited.

### 8.9 ConsoleUI

Shared helper: colour (ANSI, with VT mode enabled on Windows; no-op if unsupported), trim, validate int/date, paginated tables (10 rows), status bar, masked password, `0`/`b` back (showMenu maps 0 onto the last “Back” item so existing `default: back = true` switches keep working).

---

## 9. Audit trail end-to-end

```
User action
   → C++ AuditLog::write(userId, action, target, success)
   → INSERT audit_log (prepared)
   → plus DB triggers for evidence soft-delete and case status
```

`result` is only `SUCCESS` or `FAIL`. `target` is a short string such as `username=admin` or `evidence_id=12 match`.

Login failures for unknown usernames use `user_id` NULL (FK). Known users get their id even on FAIL so lockout investigations work.

---

## 10. GUI internals (Windows)

`GuiApp` owns the same DatabaseConnection / Users / Backup objects. Win32 controls (list-view, edits F1–F8, combos, buttons). Module name in `module_` chooses which SQL `refresh()` / `doCreate()` runs. Nav buttons store a `std::string*` in `GWLP_USERDATA`. A 30-second timer calls `onTimer()` for session timeout.

CMake target `securetrace_gui` is `WIN32` (no console), links `comctl32`. UNICODE is defined so list-view macros match wide APIs.

---

## 11. Tests

| Binary | What it proves |
|--------|----------------|
| `securetrace_tests` | Injection strings do not log in or drop `users`; 3 failures lock; viewer permission matrix; two different texts produce different SHA-256 |
| `securetrace_feature_tests` | Logins, RBAC, CRUDS, hash create/verify, close-case unverified detection, custody INSERT and blocked UPDATE/DELETE, analysis SQL, report SQL, export files, seed counts |

They load `config.ini` and talk to a live database. Feature tests mark `TEST_FEATURE*` rows inactive on cleanup because custody GRANT forbids deleting transfer rows.

---

## 12. Files at runtime

| Path | Role |
|------|------|
| `config/config.ini` | Secrets (gitignored) |
| `logs/app.log` | Connector errors, login, timeouts |
| `reports/*.txt,*.csv` | Report exports |
| `reports/backups/backup_*` | Admin dumps |
| `sql/*.sql` | Schema, grants, seed, phase-2 alter |

---

## 13. Trust boundaries (what can go wrong)

- Anyone with the `securetrace_app` password can read/write cases and evidence through any client, not only this binary. Protect `config.ini`.
- `--demo` is powerful (reload as root). Do not enable it on a shared production server.
- SHA-256 here is **integrity of the copy you hash**, not a chain to a hardware write-blocker.
- Custody person names are free text; they are not FK-tied to `users`.
- GUI and console share the DB; they do not share an in-memory session.

---

## 14. Map of source files

| File | Responsibility |
|------|----------------|
| `src/main.cpp` | Console loop, dashboard, --demo/--help, wiring |
| `src/Config.cpp` | INI loader |
| `src/Logger.cpp` | Timestamped log file |
| `src/DatabaseConnection.cpp` | Connector/C++, prepared statements, `@app_user_id` |
| `src/ConsoleUI.cpp` | Menus, colour, tables, input |
| `src/AuditLog.cpp` | INSERT audit_log |
| `src/Users.cpp` | Login, PBKDF2, lockout, RBAC, user CRUDS |
| `src/Cases.cpp` / `Evidence.cpp` / `Investigators.cpp` / `CustodyTransfers.cpp` | Domain CRUDS |
| `include/HashUtil.h` | OpenSSL SHA-256 |
| `src/Analysis.cpp` | Named SQL calculations |
| `src/Reports.cpp` | Report generator + export |
| `src/Backup.cpp` | CSV/SQL dump |
| `src/GuiApp.cpp` | Win32 UI |
| `sql/schema.sql` | DDL + triggers |
| `sql/create_app_user.sql` | GRANTs |
| `sql/seed.sql` | Demo rows |

That is the system from process start, through auth and SQL, to files on disk.
