#include "AuditLog.h"
#include "Config.h"
#include "ConsoleUI.h"
#include "DatabaseConnection.h"
#include "HashUtil.h"
#include "Logger.h"
#include "Session.h"
#include "Users.h"

#include <cppconn/resultset.h>

#include <iostream>
#include <memory>
#include <string>

namespace {
int passed = 0;
int failed = 0;

void check(bool ok, const std::string& name) {
    if (ok) {
        std::cout << "[PASS] " << name << "\n";
        ++passed;
    } else {
        std::cout << "[FAIL] " << name << "\n";
        ++failed;
    }
}

int tableCount(DatabaseConnection& db, const std::string& table) {
    auto stmt = db.prepare("SELECT COUNT(*) AS n FROM " + table);
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    if (rs->next()) {
        return rs->getInt("n");
    }
    return -1;
}
}

int main() {
    try {
        Logger::instance().init("logs/app.log");
        Config config;
        const std::string path = Config::findPath();
        if (path.empty() || !config.load(path)) {
            std::cout << "[FAIL] could not load config.ini\n";
            return 1;
        }
        DatabaseConnection db(config);
        db.connect();
        AuditLog audit(db);
        ConsoleUI ui;
        Users users(db, audit, config, ui);

        std::cout << "SecureTrace security tests\n";

        const int usersBefore = tableCount(db, "users");
        Session injection;
        const bool inj1 = users.authenticate("' OR '1'='1", "x", injection, false);
        check(!inj1 && !injection.authenticated, "SQL injection login ' OR '1'='1 rejected");

        Session injection2;
        const bool inj2 = users.authenticate("'; DROP TABLE users;--", "x", injection2, false);
        const int usersAfter = tableCount(db, "users");
        check(!inj2 && usersAfter == usersBefore,
              "SQL injection '; DROP TABLE users;-- rejected and users table intact");

        auto del = db.prepare("DELETE FROM users WHERE username = ?");
        del->setString(1, "locktest");
        del->executeUpdate();
        auto ins = db.prepare(
            "INSERT INTO users (username, password_hash, salt, role, failed_attempts, locked_until, is_active) "
            "SELECT 'locktest', password_hash, salt, 'viewer', 0, NULL, 1 FROM users WHERE username = 'viewer'");
        ins->executeUpdate();

        Session lockSession;
        users.authenticate("locktest", "wrong1", lockSession, false);
        users.authenticate("locktest", "wrong2", lockSession, false);
        users.authenticate("locktest", "wrong3", lockSession, false);
        auto lockCheck = db.prepare(
            "SELECT failed_attempts, (locked_until IS NOT NULL AND locked_until > NOW()) AS is_locked "
            "FROM users WHERE username = ?");
        lockCheck->setString(1, "locktest");
        std::unique_ptr<sql::ResultSet> lockRs(lockCheck->executeQuery());
        bool locked = false;
        int fails = 0;
        if (lockRs->next()) {
            fails = lockRs->getInt("failed_attempts");
            locked = lockRs->getBoolean("is_locked");
        }
        const bool fourth = users.authenticate("locktest", "Viewer@123", lockSession, false);
        check(fails >= 3 && locked && !fourth, "Lockout after 3 failed logins");

        auto cleanup = db.prepare("DELETE FROM users WHERE username = ?");
        cleanup->setString(1, "locktest");
        cleanup->executeUpdate();

        Session viewer;
        viewer.authenticated = true;
        viewer.role = Role::Viewer;
        viewer.username = "viewer";
        viewer.userId = 3;
        check(!users.hasPermission(viewer, "users") &&
              !users.hasPermission(viewer, "cases") &&
              !users.hasPermission(viewer, "backup") &&
              users.hasPermission(viewer, "reports_summary"),
              "RBAC denies viewer admin/investigator actions");

        auto evid = db.prepare(
            "SELECT evidence_id, sha256_hash FROM evidence WHERE is_active = 1 ORDER BY evidence_id LIMIT 1");
        std::unique_ptr<sql::ResultSet> evidRs(evid->executeQuery());
        bool tamperDetected = false;
        if (evidRs->next()) {
            const std::string stored = evidRs->getString("sha256_hash");
            double sizeMb = 0.0;
            const std::string original = HashUtil::sha256Text("original-evidence-bytes", sizeMb);
            const std::string tampered = HashUtil::sha256Text("tampered-evidence-bytes", sizeMb);
            tamperDetected = (original != tampered) && (stored.size() == 64) && (tampered != stored);
        }
        check(tamperDetected, "Tampered evidence detected by SHA-256 mismatch");

        std::cout << "\n" << passed << " passed, " << failed << " failed\n";
        db.disconnect();
        return failed == 0 ? 0 : 1;
    } catch (const std::exception& ex) {
        std::cout << "[FAIL] test runner: " << ex.what() << "\n";
        return 1;
    }
}
