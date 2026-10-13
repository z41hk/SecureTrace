# How to run SecureTrace from scratch

This is a complete first-time setup: get the code, install tools, create the database, configure the app, build it, and log in. No previous SecureTrace install is assumed.

If the app is already built on this machine, skip to [Run the program](#8-run-the-program).

---

## What you will have at the end

- MySQL or MariaDB running with database `securetrace`
- App login user `securetrace_app` (not root)
- `config/config.ini` pointing at that database
- `securetrace` console program (and optionally `securetrace_gui` on Windows)
- Three demo accounts: admin, investigator, viewer

---

## 1. Get the source code

```bash
git clone https://github.com/z41hk/SecureTrace.git
cd SecureTrace
```

Or download the ZIP from GitHub and extract it. All later commands are run from this folder.

---

## 2. Install software

You need four things: a C++ compiler, CMake, a MySQL-compatible server, the C++ connector, and OpenSSL.

### Windows

1. **Visual Studio 2022 Build Tools** (or full Visual Studio) with the **Desktop development with C++** workload.
2. **CMake** 3.16 or newer (Build Tools often already include it).
3. **MySQL 8** or **MariaDB** (XAMPP’s MySQL/MariaDB is fine).
4. **MySQL Connector/C++** JDBC/classic (the package that contains `mysql_driver.h` and `mysqlcppconn.lib`).  
   Typical path: `C:\Program Files\MySQL\MySQL Connector C++ 8.0`
5. **OpenSSL for Windows** (Win64), for example from [Shining Light](https://slproweb.com/products/Win32OpenSSL.html).  
   Typical path: `C:\Program Files\OpenSSL-Win64`

Start the database:

- XAMPP: open XAMPP Control Panel → Start **MySQL**, or run `mysql_start.bat`.
- MySQL as a Windows service: `net start MySQL80` (name varies).

Check that port **3306** is listening.

### Linux (Debian/Ubuntu)

```bash
sudo apt update
sudo apt install g++ cmake libssl-dev libmysqlcppconn-dev mysql-server git
sudo systemctl start mysql
```

On some distros the connector package is `libmysqlcppconn-dev`. You need the **classic/JDBC** headers (`mysql_driver.h`), not only the X DevAPI.

---

## 3. Create the database (as MySQL root)

You must use a MySQL **administrator** account (usually `root`) for this step only. The running app will **not** use root.

Open a SQL client:

**Windows (XAMPP example)**

```bat
"D:\xampp\mysql\bin\mysql.exe" -u root
```

If root has a password:

```bat
"D:\xampp\mysql\bin\mysql.exe" -u root -p
```

**Linux**

```bash
sudo mysql
```

In the `mysql>` prompt, load the three scripts **in this order**. Use the full path to your clone if you are not already in the project folder.

```sql
SOURCE sql/schema.sql;
SOURCE sql/create_app_user.sql;
SOURCE sql/seed.sql;
```

Windows example with a full path:

```sql
SOURCE C:/Users/You/SecureTrace/sql/schema.sql;
SOURCE C:/Users/You/SecureTrace/sql/create_app_user.sql;
SOURCE C:/Users/You/SecureTrace/sql/seed.sql;
```

What each file does:

| File | Effect |
|------|--------|
| `sql/schema.sql` | Creates database `securetrace`, all tables, indexes, CHECKs, foreign keys, triggers |
| `sql/create_app_user.sql` | Creates MySQL user `securetrace_app` with a **least-privilege** GRANT (not root) |
| `sql/seed.sql` | Loads 5 investigators, 15 cases, 40 evidence items, 60 custody rows, 3 app logins |

Confirm:

```sql
USE securetrace;
SHOW TABLES;
SELECT username, role FROM users;
SELECT COUNT(*) FROM cases;
SELECT COUNT(*) FROM evidence;
SELECT COUNT(*) FROM custody_transfers;
EXIT;
```

You should see six tables and users `admin`, `nurul`, `viewer`.

### App MySQL user (important)

`create_app_user.sql` creates:

- User: `securetrace_app`@`localhost`
- Password: `ChangeMe_AppUser_1`

Privileges:

- investigators, cases, evidence, users → SELECT, INSERT, UPDATE, DELETE
- custody_transfers, audit_log → SELECT, INSERT only (chain of custody cannot be silently edited)

If you change that password, change `config/config.ini` to match.

If `CREATE USER` says the user already exists, you can still run the `GRANT` lines, or:

```sql
ALTER USER 'securetrace_app'@'localhost' IDENTIFIED BY 'ChangeMe_AppUser_1';
FLUSH PRIVILEGES;
```

### Optional: old Phase 1 database only

If someone created the database before `cases.is_active` existed, run once:

```sql
SOURCE sql/alter_phase2.sql;
```

A fresh `schema.sql` already includes that column. Skip this on a new install.

---

## 4. Application config file

Never commit real passwords. The repo has an example only.

```bash
cp config/config.example.ini config/config.ini
```

Windows (Command Prompt):

```bat
copy config\config.example.ini config\config.ini
```

Edit `config/config.ini`:

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

| Setting | Must match |
|---------|------------|
| `host` / `port` | Your MySQL listen address (usually 127.0.0.1:3306) |
| `name` | `securetrace` |
| `user` / `password` | `securetrace_app` and the password in `create_app_user.sql` |

If MySQL root works but the app says it cannot connect, you are probably still pointing `config.ini` at `root`, or the password does not match `securetrace_app`.

---

## 5. Build the program

### Windows

From the project root:

```bat
build.bat
```

If Connector or OpenSSL are not in the default folders:

```bat
set MYSQL_CONNECTOR_DIR=C:\Program Files\MySQL\MySQL Connector C++ 8.0
set OPENSSL_ROOT_DIR=C:\Program Files\OpenSSL-Win64
build.bat
```

Or CMake by hand:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
  -DMYSQL_CONNECTOR_DIR="C:\Program Files\MySQL\MySQL Connector C++ 8.0" ^
  -DOPENSSL_ROOT_DIR="C:\Program Files\OpenSSL-Win64"
cmake --build build --config Release
```

Output:

```
build\Release\securetrace.exe
build\Release\securetrace_gui.exe          (optional GUI)
build\Release\securetrace_tests.exe
build\Release\securetrace_feature_tests.exe
```

**Copy DLLs** next to `securetrace.exe` if Windows cannot start it:

- `mysqlcppconn*.dll` from the Connector `lib64` folder
- OpenSSL `libssl` / `libcrypto` DLLs if needed

Also copy `config\config.ini` into `build\Release\config\` (or run from the project root so `config/config.ini` is found). The CMake post-build step copies `config.example.ini` into the output `config/` folder — you still need a real `config.ini`.

### Linux

```bash
chmod +x build.sh
./build.sh
```

Binary: `./build/securetrace`

If CMake cannot find the connector:

```bash
cmake -S . -B build -DMYSQL_CONNECTOR_DIR=/usr
cmake --build build
```

---

## 6. Where to run from

The program looks for `config/config.ini` in the **current working directory** (then a few parent folders).

**Good**

```bat
cd C:\path\to\SecureTrace
build\Release\securetrace.exe
```

or copy `config`, `logs`, `reports` next to the exe and `cd` there.

**Bad**

Double-clicking the exe from a random folder with no `config/` → “config.ini is missing”.

Create empty folders if needed:

```
mkdir logs
mkdir reports
```

---

## 7. Check the database from the command line

Before starting the app:

```bash
mysql -u securetrace_app -pChangeMe_AppUser_1 -e "USE securetrace; SELECT username, role FROM users;"
```

Windows XAMPP:

```bat
"D:\xampp\mysql\bin\mysql.exe" -u securetrace_app -pChangeMe_AppUser_1 -e "USE securetrace; SELECT username, role FROM users;"
```

If this fails, fix MySQL user/password/host before debugging C++.

---

## 8. Run the program

### Console

```bat
securetrace.exe
securetrace.exe --help
```

Linux:

```bash
./build/securetrace
./build/securetrace --help
```

### Demo reload (optional)

Rebuilds the database from `schema.sql` + `seed.sql`. Needs the `mysql` client and **root** access. Confirm with `y`.

```bat
securetrace.exe --demo
```

### Windows GUI (if you built it)

```bat
securetrace_gui.exe
```

Same `config.ini` and same logins. Run from a folder that contains `config/`.

### Log in

| Username | Password    | Role         |
|----------|-------------|--------------|
| `admin`  | `Admin@123` | full access  |
| `nurul`  | `Invest@123`| investigator |
| `viewer` | `Viewer@123`| read-only    |

After login you should see the **dashboard**, then the main menu. Banner line: `UTeM | Faculty of AI and Cyber Security (FAIX)`.

How to use menus: [USER_MANUAL.md](USER_MANUAL.md).

---

## 9. Tests (optional)

Run from a directory that can load `config/config.ini`, with MySQL up:

```bat
build\Release\securetrace_tests.exe
build\Release\securetrace_feature_tests.exe
```

Linux:

```bash
./build/securetrace_tests
./build/securetrace_feature_tests
```

---

## 10. Troubleshooting

| Message / symptom | Cause | Fix |
|-------------------|--------|-----|
| Startup check failed: config.ini is missing | Wrong working directory, or file not copied | Copy `config.example.ini` → `config.ini`; `cd` to the project (or exe folder with `config\`) |
| Database is unreachable | MySQL stopped, wrong host/port | Start MySQL; check 3306; ping with `mysql -u securetrace_app ...` |
| Access denied for user | Wrong password or user in `config.ini` | Match `create_app_user.sql`; do not use root in the app config |
| Unknown database `securetrace` | `schema.sql` not loaded | Run section 3 again |
| Table `cases` doesn't exist | Incomplete SOURCE | Re-run `schema.sql` then `seed.sql` |
| `mysql_driver.h` / connector not found | CMake cannot see Connector/C++ | Set `-DMYSQL_CONNECTOR_DIR=` to the install root that contains `include` and `lib64` |
| OpenSSL not found | OpenSSL not installed | Install Win64 OpenSSL or `libssl-dev`; set `OPENSSL_ROOT_DIR` |
| `VCRUNTIME` / missing DLL | Connector or OpenSSL DLL not beside the exe | Copy `mysqlcppconn*.dll` (and OpenSSL DLLs) next to `securetrace.exe` |
| Account locked | 3 bad passwords | Wait 5 minutes, or log in as admin → Users → Unlock |
| `--demo` fails | `mysql` not on PATH, or not root | Use full path to `mysql.exe`; run as admin/root |

Technical detail is appended to `logs/app.log`.

---

## 11. Checklist

- [ ] Source cloned
- [ ] Compiler, CMake, MySQL, Connector/C++, OpenSSL installed
- [ ] MySQL running on 3306
- [ ] `SOURCE schema.sql` then `create_app_user.sql` then `seed.sql`
- [ ] `SELECT username FROM securetrace.users` returns three rows
- [ ] `config/config.ini` exists and uses `securetrace_app`
- [ ] Project builds
- [ ] DLLs copied (Windows)
- [ ] App started from a folder that has `config/`
- [ ] Login `admin` / `Admin@123` works

You are done. Next: [USER_MANUAL.md](USER_MANUAL.md) for every menu, or [HOW_IT_WORKS.md](HOW_IT_WORKS.md) for internals.
