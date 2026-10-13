# SecureTrace User Manual

How to use every feature of SecureTrace. This is the operator handbook, not the source-code guide. For architecture, database design, hashing, and SQL internals, see [HOW_IT_WORKS.md](HOW_IT_WORKS.md).

---

## 1. What this software is

SecureTrace is a **digital evidence and case management** console (and optional Windows GUI) for UTeM FTKKI. You record cases, bag evidence, log who held an item, run analysis queries, and export reports.

It does **not** scan networks, extract data from devices, or run exploits. Integrity checking only hashes a file or text you already have.

---

## 2. Before you start

1. MySQL or MariaDB must be running.
2. The database must already be created (`sql/schema.sql`, `sql/create_app_user.sql`, `sql/seed.sql`).
3. `config/config.ini` must exist next to how you run the program (copy from `config/config.example.ini`).
4. Start the app from a folder that contains `config/`, `logs/`, and `reports/` (the build copies these next to the `.exe` on Windows).

### Demo logins

| Username | Password    | Role         | Use this to… |
|----------|-------------|--------------|----------------|
| `admin`  | `Admin@123` | admin        | Everything, including users, audit, backup, restore |
| `nurul`  | `Invest@123`| investigator | Day-to-day case and evidence work |
| `viewer` | `Viewer@123`| viewer       | Read-only summaries (sensitive fields hidden) |

Change these passwords in a real workshop.

### Start commands

```
securetrace
securetrace --help
securetrace --demo
```

`--demo` drops and reloads `schema.sql` + `seed.sql`. It needs a MySQL **root** client on the machine. Confirm with `y` before it runs.

Windows GUI (if built): `securetrace_gui.exe` — same logins, left-hand module list, Create / Update / Delete / Search / Verify on the toolbar.

---

## 3. Keys and colours

| Key / prompt | Meaning |
|--------------|---------|
| Number | Choose that menu item |
| `0` or `b` | Go back |
| `c` | Cancel the current form field |
| `y` / `n` | Confirm or abort delete / close / restore |
| `n` / `p` | Next / previous page on tables (10 rows per page) |
| Enter | Finish paging, or continue after a pause |

Colours (plain text if the terminal cannot do colour):

- Green `[OK]` — success
- Red `[ERROR]` — rejected or failed
- Yellow `[WARN]` — warning (startup, `--demo`)
- Cyan status bar — user, role, current time (after login)

---

## 4. Login, dashboard, session

### Login

Welcome menu → **Login**. Type username (3–32 characters). Password is masked with `*`.

What you may see:

- Invalid username or password (same message either way)
- Account is disabled
- Account is locked (3 failures → locked 5 minutes)

A successful login writes `LOGIN` / `SUCCESS` to the audit log.

### Dashboard (after every successful login)

| Metric | Meaning |
|--------|---------|
| Open / in-progress cases | Active cases not yet Closed |
| Overdue cases | Not closed, deadline before today |
| Unverified evidence | Active items that have not passed hash verify |
| Last login | Previous successful login time for this user |

You can open the dashboard again from **Main Menu → Dashboard**.

### Session timeout

Idle **10 minutes** → auto logout. Log in again.

### Help and About

- Welcome → **Help** or **About**
- Main menu → **Help** or **About**
- Command line: `securetrace --help`

---

## 5. Main menu by role

After login the status bar shows `User | Role | time`.

**Admin** sees: Cases, Evidence, Investigators, Custody Transfers, User Management, Audit Log, Backup / Export, Analysis, Reports, Dashboard, Help, About, Logout, Exit.

**Investigator** sees: Cases, Evidence, Custody, Analysis, Reports, Dashboard, Help, About, Logout, Exit.

**Viewer** sees: Analysis & Calculations, Summary Reports, Dashboard, Help, About, Logout, Exit. Description and storage location show as `********`.

If a menu item is missing, your role cannot use it. The program also checks permission again inside each module.

---

## 6. Cases

**Who:** admin, investigator.

Menu: Create, List all, Read by ID, Update, Delete (soft), Search, Restore inactive (admin), Back.

### Create

You will be asked for:

1. Title (3–150 characters)
2. Type: Fraud / Data Breach / Insider Threat / Malware / Other
3. Priority: Low / Medium / High / Critical
4. Opened date (`YYYY-MM-DD`)
5. Deadline (`YYYY-MM-DD`) — cannot be before opened date
6. Assigned investigator ID (a table of investigators is shown first)

New cases start as **Open**. `closed_date` stays empty.

### List all

Optional filter by status and type. Optional sort: ID, Title, Opened date, Deadline, Priority. Only **active** cases. Long titles are truncated; use **n/p** if there are more than 10 rows.

### Read by ID

Shows one case including closed date, investigator name, and active flag.

### Update

Enter the case ID. Leave a field blank to keep the current value. You may change title, type, priority, deadline, investigator, and status.

**Closing a case**

- Status **Closed** requires a closed date (blank = today). Closed date cannot be before opened date.
- If the case still has **unverified active evidence**, an investigator is blocked.
- An **admin** can override. That writes `CASE_CLOSE_OVERRIDE` to the audit log. Confirm with `y`.

### Delete (soft)

Marks `is_active = 0`. The row is not removed. Confirm with `y`. Inactive cases drop off **List all** but still appear in **Search** with Active = no.

### Search

Optional Case ID, status, type, and a **keyword** that matches title, type, status, or investigator name (partial match).

### Restore inactive (admin)

Enter an inactive case ID, confirm `y`. The case becomes active again.

---

## 7. Evidence

**Who:** admin, investigator.

Menu: Create, List all, Read by ID, Update, Delete (soft), Search, Verify integrity, Restore inactive (admin), Back.

### Create

1. Case ID — must be an **active** case
2. Description
3. Type: Laptop / USB / Phone / Disk Image / Log File / Other
4. Source: **file path** or **typed text** — SHA-256 is computed and stored; size in MB is stored (minimum 0.01)
5. Collected date
6. Storage location

`verified` starts as no.

### List all

Optional filter by type and verified (yes/no). Optional sort: ID, Case ID, Type, Size, Collected date.

### Read by ID

Full row including hash, location, verified, active.

### Update

You may change description, type, location, collected date. **The hash is not edited here.** Use Verify if the bits changed.

### Delete (soft)

Sets `is_active = 0`. Confirm `y`. You cannot add custody transfers for inactive evidence.

### Search

Optional evidence ID, case ID, type, hash prefix, and a **keyword** over description, type, hash, and storage location.

### Verify integrity

1. Evidence ID (must be active)
2. The **same** file path or text used at collection

- Match → `verified = yes`, audit `HASH_VERIFY` SUCCESS  
- Mismatch → `verified = no`, FAIL, stored vs computed hashes printed  

This only proves the file/text you present still matches the stored digest.

### Restore inactive (admin)

Same idea as cases: ID + confirm `y`.

---

## 8. Investigators

**Who:** admin only.

Menu: Create, List all, Read by ID, Update, Delete, Search, Back.

- **Create** — name, role, specialty, department
- **List / Read / Search** — search by ID, name contains, specialty contains
- **Update** — blank keeps the current value
- **Delete** — **permanent**. Blocked if that person is still assigned to any case. Reassign cases first.

---

## 9. Custody transfers

**Who:** admin, investigator.

This module is **append-only**. There is no Update or Delete on the menu, and the database user cannot UPDATE or DELETE these rows.

Menu: Create, List all, Read by ID, Search, Back.

### Create

1. Evidence ID — evidence must exist and be **active**
2. From person
3. To person
4. Datetime `YYYY-MM-DD HH:MM:SS` (blank = now)
5. Reason (at least 3 characters)

### List / Read / Search

Search by transfer ID, evidence ID, from-person contains, to-person contains.

Use this when an item moves (intake, locker, analyst, NAS). Do not “fix” a bad row by editing it — add a new transfer that explains the correction.

---

## 10. User management

**Who:** admin only.

Menu: Create, List all, Read by ID, Update, Disable (soft), Search, Unlock account, Back.

### Create

- Username: 3–32 letters, digits, underscore
- Password twice; must be 8+ characters with a digit and a symbol
- Role: admin / investigator / viewer

The password is never stored in plain text.

### Update

Change role (not your own), toggle active (not your own), optionally reset password.

### Disable

Soft-delete: `is_active = 0`. That user cannot log in. You cannot disable yourself.

### Unlock

Clears `failed_attempts` and `locked_until` after a lockout.

### Search

ID, username contains, exact role.

---

## 11. Analysis & Calculations

**Who:** admin and investigator (full). Viewer can open analysis; description/location stay masked.

Every item is a **named menu option**. Each screen prints one sentence explaining the query, then a table.

### A. Unary (one column)

| Menu item | What you see |
|-----------|----------------|
| COUNT | Totals of cases, evidence items, investigators, with the column name |
| MAX | Largest `size_mb`, latest case deadline, latest custody datetime |
| MIN | Earliest case opened date, smallest evidence size |

### B. Grouping & aggregation

| Menu item | What you see |
|-----------|----------------|
| COUNT + GROUP BY | Cases per type, per priority, per opened month |
| SUM + GROUP BY | Total evidence size (MB) per case |
| AVG + GROUP BY | Average case duration in days per type |
| GROUP BY + HAVING | Investigators with **3 or more** open/in-progress cases |

### C. Join

| Menu item | What you do |
|-----------|-------------|
| Full case file | Optional case ID (blank = all). Cases + investigator + evidence |
| Chain of custody | Enter evidence ID. Transfers plus user who logged CREATE, if any |

### D. Subquery

| Menu item | What you see |
|-----------|----------------|
| No evidence | Active cases with no active evidence rows |
| Above-average load | Investigators whose open-case count > average investigator |
| Never transferred | Active evidence with no custody row |
| Deadline before average | Active cases whose deadline is earlier than the average deadline |

### E. Decision support

| Menu item | What you see |
|-----------|----------------|
| Workload score | Open cases weighted Low 1, Medium 2, High 3, Critical 5. **Recommend next investigator** = lowest score |
| Integrity rate | Verified / total active evidence × 100 |
| Days remaining | Deadline minus today. **Negative = overdue** |

---

## 12. Reports

**Investigator / admin:** Report Generator (all reports; audit is admin).  
**Viewer:** Summary Reports only — items 1, 4, and 5.

After the table you are asked to **export**. You first **preview** the report, then choose a date filter and format.

### Date filters

- All dates
- Today
- This week (Monday through today)
- This month
- Custom range (`YYYY-MM-DD` from / to)

### Formats

TXT, CSV, or both. Files go under `reports/` with a timestamp, for example:

```
reports/case_status_20261013_143015.txt
reports/case_status_20261013_143015.csv
```

The full path is printed. Audit action: `REPORT_EXPORT`.

### Report list

| # | Name | Contents |
|---|------|----------|
| 1 | Case status summary | Opened vs closed counts by month |
| 2 | Summary lists | Top 5 busiest investigators, overdue cases, unverified evidence |
| 3 | Chain-of-custody timeline | Enter evidence ID; transfer history |
| 4 | Bar chart: cases per type | `#` bars scaled to the largest count, value shown |
| 5 | Trend | Cases per month (bar) and priority share in % (bar) |
| 6 | Audit log | Admin. Entries plus failed logins per day |

Viewer cannot open 2, 3, or 6.

---

## 13. Audit log (admin)

Main menu → **Audit Log**, or Reports → Audit log report.

Filter by date, then see timestamp, username, action, target, SUCCESS/FAIL. The second table is failed logins grouped by day.

Typical actions: `LOGIN`, `LOGOUT`, `CREATE`, `UPDATE`, `DELETE`, `HASH_VERIFY`, `CASE_CLOSE_OVERRIDE`, `REPORT_EXPORT`, `BACKUP_EXPORT`, plus trigger rows `EVIDENCE_SOFT_DELETE` and `CASE_STATUS_CHANGE`.

---

## 14. Backup / Export (admin)

Main menu → **Backup / Export** → Export all tables.

Writes a folder:

```
reports/backups/backup_YYYYMMDD_HHMMSS/
```

Each table has a `.csv` and a `.sql` (INSERT statements). Open that folder in Explorer. This is a data dump, not a live `mysqldump` of the server.

---

## 15. Windows GUI (optional)

Same account and same rules.

- Left buttons: Cases, Evidence, Investigators, Custody, Users, Audit, Backup, Analysis, Reports (role filtered)
- Top: Create, Update, Delete, Search, Verify, Refresh, Export, Backup, Logout
- **F1–F8** are form fields; combos are type / priority / status (or evidence type / user role)
- Analysis buttons AGGREGATION … DECISION; report buttons R1–R6
- Viewer still cannot run admin actions; reports 2, 3, 6 are blocked
- Click a list row to load it into F1–F8

Evidence Create/Verify: put the **file path or text** in **F5**.

---

## 16. Troubleshooting

| Symptom | What to try |
|---------|-------------|
| Config missing | Copy `config/config.example.ini` to `config/config.ini` |
| Database unreachable | Start MySQL/XAMPP, check host/port/user/password |
| Locked account | Wait 5 minutes, or admin → Users → Unlock |
| Cannot close a case | Verify remaining evidence, or admin override |
| Cannot add custody | Evidence is missing or soft-deleted — restore first |
| Cannot delete investigator | Still assigned to cases |
| Hash mismatch | You hashed different bytes than at collection |
| Viewer sees `********` | Intended — description and location are hidden |
| DLL missing (Windows) | Copy `mysqlcppconn*.dll` next to the exe |
| `--demo` fails | `mysql` client not on PATH, or not running as root |

Technical errors go to `logs/app.log`. The screen only shows “Operation failed” style messages.

---

## 17. Suggested workshop path

1. Login as **admin**, look at Dashboard.
2. List cases, open Analysis A–E so the lecturer can see each named query.
3. Create evidence from typed text, Verify with the same text, then with different text.
4. Close a case that still has unverified evidence (blocked), then override as admin.
5. Add a custody transfer; confirm there is no Update/Delete.
6. Export Report 1 as TXT and CSV; note the path.
7. Backup / Export; open the backup folder.
8. Logout, login as **viewer**, confirm menus and masking.
9. Three wrong passwords on a spare account → lockout.
