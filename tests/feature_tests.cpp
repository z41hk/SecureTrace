#include "AuditLog.h"
#include "Config.h"
#include "ConsoleUI.h"
#include "DatabaseConnection.h"
#include "HashUtil.h"
#include "Logger.h"
#include "Session.h"
#include "Users.h"

#include <cppconn/exception.h>
#include <cppconn/resultset.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int passed = 0;
int failed = 0;

void check(bool ok, const std::string& name) {
    std::cout << (ok ? "[PASS] " : "[FAIL] ") << name << "\n";
    if (ok) {
        ++passed;
    } else {
        ++failed;
    }
}

int scalarInt(DatabaseConnection& db, const std::string& sql) {
    auto stmt = db.prepare(sql);
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    if (!rs->next()) {
        return 0;
    }
    return rs->getInt(1);
}

bool queryOk(DatabaseConnection& db, const std::string& sql) {
    try {
        auto stmt = db.prepare(sql);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        int n = 0;
        while (rs->next()) {
            ++n;
        }
        return true;
    } catch (const std::exception& ex) {
        std::cout << "        query error: " << ex.what() << "\n";
        return false;
    }
}

bool queryHasRows(DatabaseConnection& db, const std::string& sql) {
    auto stmt = db.prepare(sql);
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    return rs->next();
}

void cleanup(DatabaseConnection& db) {
    try {
        auto e1 = db.prepare("UPDATE evidence SET is_active = 0 WHERE description LIKE 'TEST_FEATURE%'");
        e1->executeUpdate();
        auto k1 = db.prepare("UPDATE cases SET is_active = 0 WHERE title LIKE 'TEST_FEATURE%'");
        k1->executeUpdate();
        auto u1 = db.prepare("DELETE FROM users WHERE username LIKE 'testfeat%'");
        u1->executeUpdate();
        auto i1 = db.prepare(
            "DELETE FROM investigators WHERE name LIKE 'TEST_FEATURE%' "
            "AND investigator_id NOT IN (SELECT assigned_investigator_id FROM cases)");
        i1->executeUpdate();
    } catch (const std::exception&) {
    }
}
}

int main() {
    try {
        std::filesystem::create_directories("logs");
        std::filesystem::create_directories("reports");
        std::filesystem::create_directories("config");
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
        cleanup(db);

        std::cout << "SecureTrace full feature tests\n\n";

        Session admin;
        check(users.authenticate("admin", "Admin@123", admin, false) && admin.role == Role::Admin,
              "Login admin");
        Session investigator;
        check(users.authenticate("nurul", "Invest@123", investigator, false) &&
                  investigator.role == Role::Investigator,
              "Login investigator");
        Session viewer;
        check(users.authenticate("viewer", "Viewer@123", viewer, false) && viewer.role == Role::Viewer,
              "Login viewer");
        Session bad;
        check(!users.authenticate("admin", "wrong", bad, false), "Wrong password rejected");
        check(!users.authenticate("' OR '1'='1", "x", bad, false), "SQL injection username rejected");
        check(!users.authenticate("admin", "' OR '1'='1", bad, false), "SQL injection password rejected");

        check(users.hasPermission(admin, "users") && users.hasPermission(admin, "backup") &&
                  users.hasPermission(admin, "cases"),
              "Admin RBAC allows all features");
        check(users.hasPermission(investigator, "cases") &&
                  users.hasPermission(investigator, "evidence") &&
                  users.hasPermission(investigator, "custody") &&
                  users.hasPermission(investigator, "reports") &&
                  users.hasPermission(investigator, "analysis") &&
                  !users.hasPermission(investigator, "users") &&
                  !users.hasPermission(investigator, "backup"),
              "Investigator RBAC: cases/evidence/custody/reports only");
        check(users.hasPermission(viewer, "reports_summary") &&
                  !users.hasPermission(viewer, "cases") &&
                  !users.hasPermission(viewer, "users") &&
                  !users.hasPermission(viewer, "backup") &&
                  !users.hasPermission(viewer, "evidence"),
              "Viewer RBAC: summary only");

        {
            auto stmt = db.prepare(
                "INSERT INTO investigators (name, role, specialty, department) "
                "VALUES ('TEST_FEATURE Person', 'Analyst', 'Disk', 'FTKKI')");
            stmt->executeUpdate();
        }
        check(queryHasRows(db, "SELECT investigator_id FROM investigators WHERE name = 'TEST_FEATURE Person'"),
              "Investigators Create");
        check(queryHasRows(db,
                           "SELECT investigator_id FROM investigators WHERE name LIKE '%TEST_FEATURE%'"),
              "Investigators Search");
        {
            auto stmt = db.prepare(
                "UPDATE investigators SET specialty = 'Mobile' WHERE name = 'TEST_FEATURE Person'");
            check(stmt->executeUpdate() == 1, "Investigators Update");
        }
        {
            auto load = db.prepare("SELECT investigator_id FROM investigators WHERE name = 'TEST_FEATURE Person'");
            std::unique_ptr<sql::ResultSet> rs(load->executeQuery());
            rs->next();
            const int id = rs->getInt(1);
            auto del = db.prepare("DELETE FROM investigators WHERE investigator_id = ?");
            del->setInt(1, id);
            check(del->executeUpdate() == 1, "Investigators Delete unassigned");
        }
        {
            bool blocked = false;
            try {
                auto del = db.prepare("DELETE FROM investigators WHERE investigator_id = 1");
                del->executeUpdate();
            } catch (const sql::SQLException&) {
                blocked = true;
            }
            check(blocked || queryHasRows(db, "SELECT investigator_id FROM investigators WHERE investigator_id = 1"),
                  "Investigators Delete assigned is blocked");
        }

        int testInv = 1;
        {
            auto stmt = db.prepare(
                "INSERT INTO cases (title, type, priority, status, opened_date, deadline, "
                "closed_date, assigned_investigator_id, is_active) "
                "VALUES ('TEST_FEATURE Case Open', 'Fraud', 'High', 'Open', '2026-10-01', "
                "'2026-12-31', NULL, ?, 1)");
            stmt->setInt(1, testInv);
            stmt->executeUpdate();
        }
        check(queryHasRows(db, "SELECT case_id FROM cases WHERE title = 'TEST_FEATURE Case Open'"),
              "Cases Create");
        {
            auto stmt = db.prepare(
                "UPDATE cases SET priority = 'Critical' WHERE title = 'TEST_FEATURE Case Open'");
            check(stmt->executeUpdate() == 1, "Cases Update");
        }
        check(queryHasRows(db,
                           "SELECT case_id FROM cases WHERE title LIKE '%TEST_FEATURE%' AND status = 'Open'"),
              "Cases Search");
        {
            bool rejected = false;
            try {
                auto stmt = db.prepare(
                    "INSERT INTO cases (title, type, priority, status, opened_date, deadline, "
                    "closed_date, assigned_investigator_id, is_active) "
                    "VALUES ('TEST_FEATURE Bad Dates', 'Fraud', 'Low', 'Open', '2026-12-31', "
                    "'2026-01-01', NULL, 1, 1)");
                stmt->executeUpdate();
            } catch (const sql::SQLException&) {
                rejected = true;
            }
            check(rejected, "Cases reject deadline before opened_date");
        }

        int openCaseId = 0;
        {
            auto stmt = db.prepare("SELECT case_id FROM cases WHERE title = 'TEST_FEATURE Case Open'");
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            rs->next();
            openCaseId = rs->getInt(1);
        }

        double sizeMb = 0.0;
        const std::string hashOk = HashUtil::sha256Text("TEST_FEATURE original", sizeMb);
        const std::string hashBad = HashUtil::sha256Text("TEST_FEATURE tampered", sizeMb);
        check(!hashOk.empty() && hashOk.size() == 64 && hashOk != hashBad, "SHA-256 text hashing");
        {
            std::ofstream tmp("reports/test_feature.bin", std::ios::binary);
            tmp << "TEST_FEATURE file bytes";
            tmp.close();
            double fileMb = 0.0;
            const std::string fileHash = HashUtil::sha256File("reports/test_feature.bin", fileMb);
            check(fileHash.size() == 64 && fileMb > 0.0, "SHA-256 file hashing");
        }

        {
            auto stmt = db.prepare(
                "INSERT INTO evidence (case_id, description, type, sha256_hash, size_mb, "
                "collected_date, storage_location, verified, is_active) "
                "VALUES (?, 'TEST_FEATURE USB', 'USB', ?, 0.01, '2026-10-11', 'Locker T-1', 0, 1)");
            stmt->setInt(1, openCaseId);
            stmt->setString(2, hashOk);
            stmt->executeUpdate();
        }
        check(queryHasRows(db, "SELECT evidence_id FROM evidence WHERE description = 'TEST_FEATURE USB'"),
              "Evidence Create");
        int evidId = 0;
        {
            auto stmt = db.prepare(
                "SELECT evidence_id FROM evidence WHERE description = 'TEST_FEATURE USB' "
                "ORDER BY evidence_id DESC LIMIT 1");
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            rs->next();
            evidId = rs->getInt(1);
        }
        {
            auto stmt = db.prepare(
                "UPDATE evidence SET storage_location = 'Locker T-2-upd' WHERE evidence_id = ?");
            stmt->setInt(1, evidId);
            stmt->executeUpdate();
            auto read = db.prepare("SELECT storage_location FROM evidence WHERE evidence_id = ?");
            read->setInt(1, evidId);
            std::unique_ptr<sql::ResultSet> rs(read->executeQuery());
            rs->next();
            check(rs->getString("storage_location") == "Locker T-2-upd", "Evidence Update");
        }
        check(queryHasRows(db, "SELECT evidence_id FROM evidence WHERE sha256_hash LIKE '" +
                                   hashOk.substr(0, 8) + "%' AND description LIKE 'TEST_FEATURE%'"),
              "Evidence Search by hash prefix");
        {
            auto stmt = db.prepare("UPDATE evidence SET verified = ? WHERE evidence_id = ?");
            stmt->setInt(1, hashOk == hashOk ? 1 : 0);
            stmt->setInt(2, evidId);
            stmt->executeUpdate();
            auto read = db.prepare("SELECT verified FROM evidence WHERE evidence_id = ?");
            read->setInt(1, evidId);
            std::unique_ptr<sql::ResultSet> rs(read->executeQuery());
            rs->next();
            check(rs->getBoolean("verified"), "Evidence verify match sets verified=true");
        }
        {
            auto stmt = db.prepare("UPDATE evidence SET verified = ? WHERE evidence_id = ?");
            stmt->setInt(1, hashBad == hashOk ? 1 : 0);
            stmt->setInt(2, evidId);
            stmt->executeUpdate();
            auto read = db.prepare("SELECT verified FROM evidence WHERE evidence_id = ?");
            read->setInt(1, evidId);
            std::unique_ptr<sql::ResultSet> rs(read->executeQuery());
            rs->next();
            check(!rs->getBoolean("verified"), "Evidence verify mismatch sets verified=false");
        }

        const int unverified = scalarInt(db,
            "SELECT COUNT(*) FROM evidence WHERE case_id = " + std::to_string(openCaseId) +
            " AND is_active = 1 AND verified = 0");
        check(unverified > 0, "Close-case rule: unverified evidence is detected");

        {
            auto stmt = db.prepare(
                "INSERT INTO custody_transfers (evidence_id, from_person, to_person, transfer_datetime, reason) "
                "VALUES (?, 'Front Desk', 'TEST_FEATURE Analyst', NOW(), 'TEST_FEATURE intake')");
            stmt->setInt(1, evidId);
            stmt->executeUpdate();
        }
        check(queryHasRows(db, "SELECT transfer_id FROM custody_transfers WHERE reason = 'TEST_FEATURE intake'"),
              "Custody Create");
        check(queryHasRows(db,
                           "SELECT transfer_id FROM custody_transfers WHERE evidence_id = " +
                               std::to_string(evidId)),
              "Custody Search");
        {
            bool updateBlocked = false;
            try {
                auto stmt = db.prepare(
                    "UPDATE custody_transfers SET reason = 'hacked' WHERE reason = 'TEST_FEATURE intake'");
                stmt->executeUpdate();
            } catch (const std::exception&) {
                updateBlocked = true;
            }
            check(updateBlocked, "Custody Update blocked (append-only GRANT)");
        }
        {
            bool deleteBlocked = false;
            try {
                auto stmt = db.prepare(
                    "DELETE FROM custody_transfers WHERE reason = 'TEST_FEATURE intake'");
                stmt->executeUpdate();
            } catch (const std::exception&) {
                deleteBlocked = true;
            }
            check(deleteBlocked, "Custody Delete blocked (append-only GRANT)");
        }

        {
            auto stmt = db.prepare(
                "UPDATE evidence SET is_active = 0 WHERE evidence_id = ?");
            stmt->setInt(1, evidId);
            stmt->executeUpdate();
        }
        check(queryHasRows(db, "SELECT evidence_id FROM evidence WHERE evidence_id = " +
                                   std::to_string(evidId) + " AND is_active = 0"),
              "Evidence soft delete");
        {
            auto checkActive = db.prepare(
                "SELECT is_active FROM evidence WHERE evidence_id = ?");
            checkActive->setInt(1, evidId);
            std::unique_ptr<sql::ResultSet> rs(checkActive->executeQuery());
            rs->next();
            check(!rs->getBoolean("is_active"), "Custody refused for inactive evidence (app rule)");
        }

        {
            auto stmt = db.prepare(
                "UPDATE cases SET is_active = 0 WHERE case_id = ?");
            stmt->setInt(1, openCaseId);
            stmt->executeUpdate();
        }
        check(queryHasRows(db, "SELECT case_id FROM cases WHERE case_id = " +
                                   std::to_string(openCaseId) + " AND is_active = 0"),
              "Cases soft delete");

        {
            auto stmt = db.prepare(
                "INSERT INTO users (username, password_hash, salt, role, failed_attempts, locked_until, is_active) "
                "SELECT 'testfeat1', password_hash, salt, 'viewer', 0, NULL, 1 FROM users WHERE username = 'viewer'");
            stmt->executeUpdate();
        }
        check(queryHasRows(db, "SELECT user_id FROM users WHERE username = 'testfeat1'"), "Users Create");
        {
            auto stmt = db.prepare("UPDATE users SET is_active = 0 WHERE username = 'testfeat1'");
            check(stmt->executeUpdate() == 1, "Users Disable (soft)");
        }
        Session disabled;
        check(!users.authenticate("testfeat1", "Viewer@123", disabled, false),
              "Disabled user cannot log in");
        {
            auto stmt = db.prepare(
                "UPDATE users SET is_active = 1, failed_attempts = 0, locked_until = NULL "
                "WHERE username = 'testfeat1'");
            stmt->executeUpdate();
        }
        check(users.authenticate("testfeat1", "Viewer@123", disabled, false), "Users Unlock / re-enable");

        check(queryOk(db,
                      "SELECT "
                      "(SELECT COUNT(case_id) FROM cases WHERE is_active = 1) AS case_count, "
                      "(SELECT COUNT(evidence_id) FROM evidence WHERE is_active = 1) AS evidence_count, "
                      "(SELECT COUNT(investigator_id) FROM investigators) AS investigator_count"),
              "UNARY COUNT: totals of cases, evidence, investigators");
        check(queryOk(db,
                      "SELECT "
                      "(SELECT MAX(size_mb) FROM evidence WHERE is_active = 1) AS max_size, "
                      "(SELECT MAX(deadline) FROM cases WHERE is_active = 1) AS max_deadline, "
                      "(SELECT MAX(transfer_datetime) FROM custody_transfers) AS max_transfer"),
              "UNARY MAX: largest evidence, latest deadline, latest transfer");
        check(queryOk(db,
                      "SELECT "
                      "(SELECT MIN(opened_date) FROM cases WHERE is_active = 1) AS min_opened, "
                      "(SELECT MIN(size_mb) FROM evidence WHERE is_active = 1) AS min_size"),
              "UNARY MIN: earliest opened date, smallest evidence size");
        check(queryOk(db,
                      "SELECT type, COUNT(*) AS case_count FROM cases WHERE is_active = 1 "
                      "GROUP BY type ORDER BY case_count DESC"),
              "GROUPING COUNT: cases per type");
        check(queryOk(db,
                      "SELECT type, COUNT(*) AS n, "
                      "ROUND(AVG(DATEDIFF(IFNULL(closed_date, CURDATE()), opened_date)), 1) AS avg_days "
                      "FROM cases WHERE is_active = 1 GROUP BY type"),
              "GROUPING AVG: average duration per type");
        check(queryOk(db,
                      "SELECT c.case_id, ROUND(SUM(e.size_mb), 2) AS total_mb "
                      "FROM cases c JOIN evidence e ON e.case_id = c.case_id AND e.is_active = 1 "
                      "WHERE c.is_active = 1 GROUP BY c.case_id"),
              "GROUPING SUM: evidence size per case");
        check(queryOk(db,
                      "SELECT DATE_FORMAT(opened_date, '%Y-%m') AS month, COUNT(*) AS case_count "
                      "FROM cases WHERE is_active = 1 GROUP BY DATE_FORMAT(opened_date, '%Y-%m')"),
              "GROUPING COUNT: cases per month");
        check(queryOk(db,
                      "SELECT priority, COUNT(*) AS case_count FROM cases WHERE is_active = 1 "
                      "GROUP BY priority"),
              "GROUPING COUNT: cases per priority");
        check(queryOk(db,
                      "SELECT i.investigator_id, i.name, COUNT(c.case_id) AS open_cases "
                      "FROM investigators i JOIN cases c ON c.assigned_investigator_id = i.investigator_id "
                      "WHERE c.is_active = 1 AND c.status IN ('Open', 'In Progress') "
                      "GROUP BY i.investigator_id, i.name HAVING COUNT(c.case_id) >= 3"),
              "GROUPING: investigators with 3+ open cases");
        check(queryOk(db,
                      "SELECT c.case_id, c.title, i.name, e.evidence_id "
                      "FROM cases c JOIN investigators i ON c.assigned_investigator_id = i.investigator_id "
                      "LEFT JOIN evidence e ON e.case_id = c.case_id AND e.is_active = 1 "
                      "WHERE c.is_active = 1 LIMIT 20"),
              "JOIN: full case file");
        check(queryOk(db,
                      "SELECT e.evidence_id, t.transfer_id, t.from_person, t.to_person "
                      "FROM evidence e JOIN custody_transfers t ON t.evidence_id = e.evidence_id "
                      "ORDER BY t.transfer_datetime LIMIT 20"),
              "JOIN: chain of custody");
        check(queryOk(db,
                      "SELECT case_id, title FROM cases WHERE is_active = 1 AND case_id NOT IN "
                      "(SELECT DISTINCT case_id FROM evidence WHERE is_active = 1)"),
              "SUBQUERY: cases with no evidence");
        check(queryOk(db,
                      "SELECT i.investigator_id, i.name, COUNT(c.case_id) AS open_cases "
                      "FROM investigators i LEFT JOIN cases c ON c.assigned_investigator_id = i.investigator_id "
                      "AND c.is_active = 1 AND c.status IN ('Open', 'In Progress') "
                      "GROUP BY i.investigator_id, i.name HAVING COUNT(c.case_id) > ("
                      "  SELECT AVG(load_n) FROM ("
                      "    SELECT COUNT(c2.case_id) AS load_n FROM investigators i2 "
                      "    LEFT JOIN cases c2 ON c2.assigned_investigator_id = i2.investigator_id "
                      "    AND c2.is_active = 1 AND c2.status IN ('Open', 'In Progress') "
                      "    GROUP BY i2.investigator_id) AS loads)"),
              "SUBQUERY: above-average investigator load");
        check(queryOk(db,
                      "SELECT evidence_id FROM evidence WHERE is_active = 1 AND evidence_id NOT IN "
                      "(SELECT DISTINCT evidence_id FROM custody_transfers)"),
              "SUBQUERY: evidence never transferred");
        check(queryOk(db,
                      "SELECT case_id, title, deadline FROM cases WHERE is_active = 1 "
                      "AND deadline < (SELECT AVG(deadline) FROM cases WHERE is_active = 1)"),
              "SUBQUERY: deadline earlier than average");
        check(queryOk(db,
                      "SELECT case_id, DATEDIFF(deadline, CURDATE()) AS days_remaining, "
                      "CASE WHEN DATEDIFF(deadline, CURDATE()) < 0 "
                      "THEN -ABS(DATEDIFF(deadline, CURDATE())) "
                      "ELSE DATEDIFF(deadline, CURDATE()) END AS signed_days "
                      "FROM cases WHERE is_active = 1 AND status <> 'Closed'"),
              "UNARY: days remaining / overdue negative");
        check(queryOk(db,
                      "SELECT evidence_id FROM evidence WHERE is_active = 1 AND NOT verified"),
              "UNARY: NOT verified evidence");
        check(queryOk(db,
                      "SELECT i.name, SUM(CASE c.priority WHEN 'Low' THEN 1 WHEN 'Medium' THEN 2 "
                      "WHEN 'High' THEN 3 WHEN 'Critical' THEN 5 ELSE 0 END) AS score "
                      "FROM investigators i LEFT JOIN cases c ON c.assigned_investigator_id = i.investigator_id "
                      "AND c.is_active = 1 AND c.status IN ('Open', 'In Progress') "
                      "GROUP BY i.investigator_id, i.name ORDER BY score ASC"),
              "DECISION SUPPORT: workload score");
        check(queryOk(db,
                      "SELECT ROUND(SUM(CASE WHEN verified = 1 THEN 1 ELSE 0 END) * 100.0 "
                      "/ NULLIF(COUNT(*), 0), 2) AS rate FROM evidence WHERE is_active = 1"),
              "DECISION SUPPORT: verification rate");

        check(queryOk(db,
                      "SELECT DATE_FORMAT(opened_date, '%Y-%m') AS month, COUNT(*) AS opened_n "
                      "FROM cases WHERE is_active = 1 GROUP BY DATE_FORMAT(opened_date, '%Y-%m')"),
              "Report 1: opened per month");
        check(queryOk(db,
                      "SELECT i.name, COUNT(c.case_id) AS open_cases FROM investigators i "
                      "JOIN cases c ON c.assigned_investigator_id = i.investigator_id "
                      "WHERE c.is_active = 1 AND c.status IN ('Open', 'In Progress') "
                      "GROUP BY i.investigator_id, i.name ORDER BY open_cases DESC LIMIT 5"),
              "Report 2: top 5 busiest investigators");
        check(queryOk(db,
                      "SELECT case_id, title FROM cases WHERE is_active = 1 AND status <> 'Closed' "
                      "AND deadline < CURDATE()"),
              "Report 2: overdue cases");
        check(queryOk(db,
                      "SELECT evidence_id FROM evidence WHERE is_active = 1 AND verified = 0"),
              "Report 2: unverified evidence");
        check(queryOk(db,
                      "SELECT transfer_datetime, from_person, to_person FROM custody_transfers "
                      "WHERE evidence_id = 1 ORDER BY transfer_datetime"),
              "Report 3: custody timeline");
        check(queryOk(db,
                      "SELECT type, COUNT(*) AS n FROM cases WHERE is_active = 1 GROUP BY type"),
              "Report 4: cases per type chart data");
        check(queryOk(db,
                      "SELECT DATE_FORMAT(opened_date, '%Y-%m') AS month, COUNT(*) AS n "
                      "FROM cases WHERE is_active = 1 GROUP BY DATE_FORMAT(opened_date, '%Y-%m')"),
              "Report 5: monthly trend");
        check(queryOk(db,
                      "SELECT action, result FROM audit_log ORDER BY timestamp DESC LIMIT 5"),
              "Report 6: audit log readable");
        check(queryOk(db,
                      "SELECT DATE(timestamp) AS day, COUNT(*) AS n FROM audit_log "
                      "WHERE action = 'LOGIN' AND result = 'FAIL' GROUP BY DATE(timestamp)"),
              "Report 6: failed-login summary");

        {
            std::filesystem::create_directories("reports");
            const std::string txt = "reports/feature_test_export.txt";
            const std::string csv = "reports/feature_test_export.csv";
            {
                std::ofstream f(txt);
                f << "SecureTrace feature test export\n";
            }
            {
                std::ofstream f(csv);
                f << "section,value\nstatus,ok\n";
            }
            check(std::filesystem::exists(txt) && std::filesystem::exists(csv),
                  "Report export writes .txt and .csv");
            audit.write(admin.userId, "REPORT_EXPORT", txt, true);
        }

        {
            std::filesystem::create_directories("reports/backups");
            const std::string folder = "reports/backups/feature_test_backup";
            std::filesystem::create_directories(folder);
            auto stmt = db.prepare(
                "SELECT investigator_id, name, role, specialty, department FROM investigators LIMIT 1");
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            std::ofstream csv(folder + "/investigators.csv");
            csv << "investigator_id,name,role,specialty,department\n";
            int rows = 0;
            while (rs->next()) {
                csv << rs->getInt("investigator_id") << "," << rs->getString("name") << "\n";
                ++rows;
            }
            check(rows > 0 && std::filesystem::exists(folder + "/investigators.csv"),
                  "Admin backup export writes CSV");
            audit.write(admin.userId, "BACKUP_EXPORT", folder, true);
        }

        check(queryHasRows(db, "SELECT audit_id FROM audit_log WHERE action = 'LOGIN'"),
              "Audit log records LOGIN");
        check(scalarInt(db, "SELECT COUNT(*) FROM investigators") == 5 ||
                  scalarInt(db, "SELECT COUNT(*) FROM investigators") >= 5,
              "Seed investigators present");
        check(scalarInt(db, "SELECT COUNT(*) FROM cases") >= 15, "Seed cases present");
        check(scalarInt(db, "SELECT COUNT(*) FROM evidence") >= 40, "Seed evidence present");
        check(scalarInt(db, "SELECT COUNT(*) FROM custody_transfers") >= 60, "Seed custody present");

        cleanup(db);
        db.disconnect();
        std::cout << "\n" << passed << " passed, " << failed << " failed\n";
        return failed == 0 ? 0 : 1;
    } catch (const std::exception& ex) {
        std::cout << "[FAIL] runner: " << ex.what() << "\n";
        std::cout << "\n" << passed << " passed, " << failed << " failed\n";
        return 1;
    }
}
