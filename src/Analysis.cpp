#include "Analysis.h"
#include "Logger.h"

#include <cppconn/exception.h>
#include <cppconn/resultset.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
void fail(ConsoleUI& ui, const std::exception& ex) {
    Logger::instance().error(std::string("analysis: ") + ex.what());
    ui.printError("Operation failed.");
}

std::string cell(sql::ResultSet& rs, const std::string& col) {
    if (rs.isNull(col)) {
        return "-";
    }
    return rs.getString(col);
}

void printRows(ConsoleUI& ui,
               const std::vector<std::string>& headers,
               const std::vector<std::vector<std::string>>& rows) {
    if (rows.empty()) {
        std::cout << "No rows.\n";
        return;
    }
    ui.printTable(headers, rows);
}
}

Analysis::Analysis(DatabaseConnection& db, ConsoleUI& ui) : db_(db), ui_(ui) {}

bool Analysis::deny(const Session& session) {
    if (!session.authenticated) {
        ui_.printError("Access denied.");
        ui_.pause();
        return true;
    }
    return false;
}

void Analysis::showMenu(const Session& session) {
    if (deny(session)) {
        return;
    }
    bool back = false;
    while (!back) {
        const int choice = ui_.showMenu("Analysis & Calculations", {
            "A. UNARY: COUNT / MAX / MIN on one column",
            "B. GROUPING & AGGREGATION: GROUP BY with SUM/AVG/COUNT/HAVING",
            "C. JOIN: case file and chain of custody",
            "D. SUBQUERY: nested SELECT queries",
            "E. DECISION SUPPORT: workload, integrity, overdue",
            "Back"
        });
        switch (choice) {
            case 1: unaryMenu(); break;
            case 2: groupingMenu(); break;
            case 3: joinMenu(session); break;
            case 4: subqueryMenu(session); break;
            case 5: decisionMenu(); break;
            default: back = true; break;
        }
    }
}

void Analysis::unaryMenu() {
    bool back = false;
    while (!back) {
        const int choice = ui_.showMenu("A. UNARY OPERATIONS", {
            "COUNT: totals of cases, evidence, investigators",
            "MAX: largest evidence, latest deadline, latest transfer",
            "MIN: earliest opened date, smallest evidence size",
            "Back"
        });
        switch (choice) {
            case 1: unaryCount(); break;
            case 2: unaryMax(); break;
            case 3: unaryMin(); break;
            default: back = true; break;
        }
    }
}

void Analysis::unaryCount() {
    ui_.printHeader("UNARY COUNT");
    try {
        // REQUIREMENT: UNARY COUNT
        std::cout << "COUNT on one column: case_id, evidence_id, investigator_id.\n";
        auto stmt = db_.prepare(
            "SELECT "
            "(SELECT COUNT(case_id) FROM cases WHERE is_active = 1) AS case_count, "
            "(SELECT COUNT(evidence_id) FROM evidence WHERE is_active = 1) AS evidence_count, "
            "(SELECT COUNT(investigator_id) FROM investigators) AS investigator_count");
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::vector<std::string>> rows;
        if (rs->next()) {
            rows.push_back({"cases.case_id", cell(*rs, "case_count")});
            rows.push_back({"evidence.evidence_id", cell(*rs, "evidence_count")});
            rows.push_back({"investigators.investigator_id", cell(*rs, "investigator_count")});
        }
        printRows(ui_, {"Column", "COUNT"}, rows);
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Analysis::unaryMax() {
    ui_.printHeader("UNARY MAX");
    try {
        // REQUIREMENT: UNARY MAX
        std::cout << "MAX on one column: evidence.size_mb, cases.deadline, custody.transfer_datetime.\n";
        auto stmt = db_.prepare(
            "SELECT "
            "(SELECT MAX(size_mb) FROM evidence WHERE is_active = 1) AS max_size, "
            "(SELECT MAX(deadline) FROM cases WHERE is_active = 1) AS max_deadline, "
            "(SELECT MAX(transfer_datetime) FROM custody_transfers) AS max_transfer");
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::vector<std::string>> rows;
        if (rs->next()) {
            rows.push_back({"evidence.size_mb", cell(*rs, "max_size")});
            rows.push_back({"cases.deadline", cell(*rs, "max_deadline")});
            rows.push_back({"custody_transfers.transfer_datetime", cell(*rs, "max_transfer")});
        }
        printRows(ui_, {"Column", "MAX"}, rows);
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Analysis::unaryMin() {
    ui_.printHeader("UNARY MIN");
    try {
        // REQUIREMENT: UNARY MIN
        std::cout << "MIN on one column: cases.opened_date, evidence.size_mb.\n";
        auto stmt = db_.prepare(
            "SELECT "
            "(SELECT MIN(opened_date) FROM cases WHERE is_active = 1) AS min_opened, "
            "(SELECT MIN(size_mb) FROM evidence WHERE is_active = 1) AS min_size");
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::vector<std::string>> rows;
        if (rs->next()) {
            rows.push_back({"cases.opened_date", cell(*rs, "min_opened")});
            rows.push_back({"evidence.size_mb", cell(*rs, "min_size")});
        }
        printRows(ui_, {"Column", "MIN"}, rows);
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Analysis::groupingMenu() {
    bool back = false;
    while (!back) {
        const int choice = ui_.showMenu("B. GROUPING & AGGREGATION", {
            "COUNT + GROUP BY: cases per type, priority, month",
            "SUM + GROUP BY: total evidence size per case",
            "AVG + GROUP BY: average case duration per type",
            "GROUP BY + HAVING: investigators with 3 or more open cases",
            "Back"
        });
        switch (choice) {
            case 1: groupingCases(); break;
            case 2: groupingEvidenceSize(); break;
            case 3: groupingAvgDuration(); break;
            case 4: groupingBusyInvestigators(); break;
            default: back = true; break;
        }
    }
}

void Analysis::groupingCases() {
    ui_.printHeader("GROUP BY COUNT");
    try {
        // REQUIREMENT: GROUPING COUNT
        std::cout << "COUNT of active cases grouped by type.\n";
        {
            auto stmt = db_.prepare(
                "SELECT type, COUNT(*) AS case_count "
                "FROM cases WHERE is_active = 1 "
                "GROUP BY type ORDER BY case_count DESC");
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            std::vector<std::vector<std::string>> rows;
            while (rs->next()) {
                rows.push_back({rs->getString("type"), cell(*rs, "case_count")});
            }
            printRows(ui_, {"Type", "COUNT"}, rows);
        }

        std::cout << "\nCOUNT of active cases grouped by priority.\n";
        {
            auto stmt = db_.prepare(
                "SELECT priority, COUNT(*) AS case_count "
                "FROM cases WHERE is_active = 1 "
                "GROUP BY priority "
                "ORDER BY FIELD(priority, 'Critical', 'High', 'Medium', 'Low')");
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            std::vector<std::vector<std::string>> rows;
            while (rs->next()) {
                rows.push_back({rs->getString("priority"), cell(*rs, "case_count")});
            }
            printRows(ui_, {"Priority", "COUNT"}, rows);
        }

        std::cout << "\nCOUNT of active cases grouped by opened month.\n";
        {
            auto stmt = db_.prepare(
                "SELECT DATE_FORMAT(opened_date, '%Y-%m') AS month, COUNT(*) AS case_count "
                "FROM cases WHERE is_active = 1 "
                "GROUP BY DATE_FORMAT(opened_date, '%Y-%m') "
                "ORDER BY month");
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            std::vector<std::vector<std::string>> rows;
            while (rs->next()) {
                rows.push_back({rs->getString("month"), cell(*rs, "case_count")});
            }
            printRows(ui_, {"Month", "COUNT"}, rows);
        }
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Analysis::groupingEvidenceSize() {
    ui_.printHeader("GROUP BY SUM");
    try {
        // REQUIREMENT: GROUPING SUM
        std::cout << "SUM of evidence.size_mb grouped by case.\n";
        auto stmt = db_.prepare(
            "SELECT c.case_id, c.title, COUNT(e.evidence_id) AS items, "
            "ROUND(SUM(e.size_mb), 2) AS total_mb "
            "FROM cases c "
            "JOIN evidence e ON e.case_id = c.case_id AND e.is_active = 1 "
            "WHERE c.is_active = 1 "
            "GROUP BY c.case_id, c.title "
            "ORDER BY total_mb DESC");
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::vector<std::string>> rows;
        while (rs->next()) {
            rows.push_back({
                cell(*rs, "case_id"),
                rs->getString("title"),
                cell(*rs, "items"),
                cell(*rs, "total_mb")
            });
        }
        printRows(ui_, {"Case", "Title", "Items", "SUM size_mb"}, rows);
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Analysis::groupingAvgDuration() {
    ui_.printHeader("GROUP BY AVG");
    try {
        // REQUIREMENT: GROUPING AVG
        std::cout << "AVG duration in days (closed_date or today minus opened_date) grouped by type.\n";
        auto stmt = db_.prepare(
            "SELECT type, COUNT(*) AS n, "
            "ROUND(AVG(DATEDIFF(IFNULL(closed_date, CURDATE()), opened_date)), 1) AS avg_days "
            "FROM cases WHERE is_active = 1 "
            "GROUP BY type ORDER BY type");
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::vector<std::string>> rows;
        while (rs->next()) {
            rows.push_back({rs->getString("type"), cell(*rs, "n"), cell(*rs, "avg_days")});
        }
        printRows(ui_, {"Type", "Cases", "AVG days"}, rows);
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Analysis::groupingBusyInvestigators() {
    ui_.printHeader("GROUP BY HAVING");
    try {
        // REQUIREMENT: GROUPING HAVING
        std::cout << "Investigators grouped by open-case count, keeping only groups with 3 or more.\n";
        auto stmt = db_.prepare(
            "SELECT i.investigator_id, i.name, COUNT(c.case_id) AS open_cases "
            "FROM investigators i "
            "JOIN cases c ON c.assigned_investigator_id = i.investigator_id "
            "WHERE c.is_active = 1 AND c.status IN ('Open', 'In Progress') "
            "GROUP BY i.investigator_id, i.name "
            "HAVING COUNT(c.case_id) >= 3 "
            "ORDER BY open_cases DESC, i.name");
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::vector<std::string>> rows;
        while (rs->next()) {
            rows.push_back({
                cell(*rs, "investigator_id"),
                rs->getString("name"),
                cell(*rs, "open_cases")
            });
        }
        printRows(ui_, {"ID", "Name", "Open cases"}, rows);
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Analysis::joinMenu(const Session& session) {
    bool back = false;
    while (!back) {
        const int choice = ui_.showMenu("C. JOIN", {
            "Full case file (cases + evidence + investigators)",
            "Chain of custody (evidence + custody + users)",
            "Back"
        });
        switch (choice) {
            case 1: fullCaseFile(session); break;
            case 2: custodyHistory(session); break;
            default: back = true; break;
        }
    }
}

void Analysis::fullCaseFile(const Session& session) {
    ui_.printHeader("JOIN: Full case file");
    std::cout << "Cases joined to assigned investigator and to evidence items.\n";
    const int caseId = ui_.readOptionalInt("Case ID (blank = all active): ", 1, 1000000);
    try {
        // REQUIREMENT: JOIN
        auto stmt = db_.prepare(
            "SELECT c.case_id, c.title, c.type, c.status, i.name AS investigator, "
            "e.evidence_id, e.type AS evidence_type, e.description, e.storage_location, "
            "e.verified, e.size_mb "
            "FROM cases c "
            "JOIN investigators i ON c.assigned_investigator_id = i.investigator_id "
            "LEFT JOIN evidence e ON e.case_id = c.case_id AND e.is_active = 1 "
            "WHERE c.is_active = 1 AND (? = 0 OR c.case_id = ?) "
            "ORDER BY c.case_id, e.evidence_id");
        const int idValue = caseId < 0 ? 0 : caseId;
        stmt->setInt(1, idValue);
        stmt->setInt(2, idValue);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::vector<std::string>> rows;
        const bool mask = (session.role == Role::Viewer);
        while (rs->next()) {
            rows.push_back({
                cell(*rs, "case_id"),
                rs->getString("title"),
                rs->getString("type"),
                rs->getString("status"),
                rs->getString("investigator"),
                cell(*rs, "evidence_id"),
                cell(*rs, "evidence_type"),
                mask ? "********" : cell(*rs, "description"),
                mask ? "********" : cell(*rs, "storage_location"),
                rs->isNull("verified") ? "-" : (rs->getBoolean("verified") ? "yes" : "no"),
                cell(*rs, "size_mb")
            });
        }
        printRows(ui_, {"Case", "Title", "Type", "Status", "Investigator",
                        "Evid", "E-Type", "Description", "Location", "Verified", "MB"}, rows);
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Analysis::custodyHistory(const Session& session) {
    ui_.printHeader("JOIN: Chain of custody");
    std::cout << "Evidence joined to custody transfers and to the user who logged the transfer.\n";
    const int evidenceId = ui_.readInt("Evidence ID: ", 1, 1000000);
    try {
        // REQUIREMENT: JOIN
        auto stmt = db_.prepare(
            "SELECT e.evidence_id, e.description, e.type, "
            "t.transfer_id, t.from_person, t.to_person, t.transfer_datetime, t.reason, "
            "u.username AS logged_by, u.role AS logger_role "
            "FROM evidence e "
            "JOIN custody_transfers t ON t.evidence_id = e.evidence_id "
            "LEFT JOIN audit_log a ON a.action = 'CREATE' "
            "  AND a.target = CONCAT('custody evidence_id=', e.evidence_id) "
            "LEFT JOIN users u ON u.user_id = a.user_id "
            "WHERE e.evidence_id = ? "
            "ORDER BY t.transfer_datetime");
        stmt->setInt(1, evidenceId);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::vector<std::string>> rows;
        const bool mask = (session.role == Role::Viewer);
        while (rs->next()) {
            rows.push_back({
                cell(*rs, "evidence_id"),
                mask ? "********" : cell(*rs, "description"),
                cell(*rs, "type"),
                cell(*rs, "transfer_id"),
                rs->getString("from_person"),
                rs->getString("to_person"),
                cell(*rs, "transfer_datetime"),
                rs->getString("reason"),
                cell(*rs, "logged_by"),
                cell(*rs, "logger_role")
            });
        }
        printRows(ui_, {"Evid", "Description", "Type", "Tx", "From", "To", "When",
                        "Reason", "Logged by", "Role"}, rows);
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Analysis::subqueryMenu(const Session& session) {
    bool back = false;
    while (!back) {
        const int choice = ui_.showMenu("D. SUBQUERY", {
            "Cases that have no evidence logged",
            "Investigators with more open cases than the average",
            "Evidence that was never transferred",
            "Cases whose deadline is earlier than the average deadline",
            "Back"
        });
        switch (choice) {
            case 1: subqueryNoEvidence(); break;
            case 2: subqueryAboveAverage(); break;
            case 3: subqueryNeverTransferred(session); break;
            case 4: subqueryEarlyDeadline(); break;
            default: back = true; break;
        }
    }
}

void Analysis::subqueryNoEvidence() {
    ui_.printHeader("SUBQUERY: no evidence");
    try {
        // REQUIREMENT: SUBQUERY
        std::cout << "Active cases whose ID is not in the evidence table.\n";
        auto stmt = db_.prepare(
            "SELECT case_id, title, status "
            "FROM cases "
            "WHERE is_active = 1 "
            "AND case_id NOT IN (SELECT DISTINCT case_id FROM evidence WHERE is_active = 1) "
            "ORDER BY case_id");
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::vector<std::string>> rows;
        while (rs->next()) {
            rows.push_back({cell(*rs, "case_id"), rs->getString("title"), rs->getString("status")});
        }
        printRows(ui_, {"ID", "Title", "Status"}, rows);
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Analysis::subqueryAboveAverage() {
    ui_.printHeader("SUBQUERY: above-average load");
    try {
        // REQUIREMENT: SUBQUERY
        std::cout << "Investigators whose open-case count is greater than the average investigator (subquery in HAVING).\n";
        auto stmt = db_.prepare(
            "SELECT i.investigator_id, i.name, COUNT(c.case_id) AS open_cases "
            "FROM investigators i "
            "LEFT JOIN cases c ON c.assigned_investigator_id = i.investigator_id "
            "  AND c.is_active = 1 AND c.status IN ('Open', 'In Progress') "
            "GROUP BY i.investigator_id, i.name "
            "HAVING COUNT(c.case_id) > ("
            "  SELECT AVG(load_n) FROM ("
            "    SELECT COUNT(c2.case_id) AS load_n "
            "    FROM investigators i2 "
            "    LEFT JOIN cases c2 ON c2.assigned_investigator_id = i2.investigator_id "
            "      AND c2.is_active = 1 AND c2.status IN ('Open', 'In Progress') "
            "    GROUP BY i2.investigator_id"
            "  ) AS loads"
            ") "
            "ORDER BY open_cases DESC");
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::vector<std::string>> rows;
        while (rs->next()) {
            rows.push_back({
                cell(*rs, "investigator_id"),
                rs->getString("name"),
                cell(*rs, "open_cases")
            });
        }
        printRows(ui_, {"ID", "Name", "Open cases"}, rows);
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Analysis::subqueryNeverTransferred(const Session& session) {
    ui_.printHeader("SUBQUERY: never transferred");
    try {
        // REQUIREMENT: SUBQUERY
        std::cout << "Active evidence whose ID is not in custody_transfers.\n";
        auto stmt = db_.prepare(
            "SELECT evidence_id, case_id, type, description "
            "FROM evidence "
            "WHERE is_active = 1 "
            "AND evidence_id NOT IN (SELECT DISTINCT evidence_id FROM custody_transfers) "
            "ORDER BY evidence_id");
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::vector<std::string>> rows;
        const bool mask = (session.role == Role::Viewer);
        while (rs->next()) {
            rows.push_back({
                cell(*rs, "evidence_id"),
                cell(*rs, "case_id"),
                rs->getString("type"),
                mask ? "********" : rs->getString("description")
            });
        }
        printRows(ui_, {"ID", "Case", "Type", "Description"}, rows);
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Analysis::subqueryEarlyDeadline() {
    ui_.printHeader("SUBQUERY: deadline before average");
    try {
        // REQUIREMENT: SUBQUERY
        std::cout << "Active cases whose deadline is earlier than the average deadline of all active cases.\n";
        auto stmt = db_.prepare(
            "SELECT case_id, title, deadline, status "
            "FROM cases "
            "WHERE is_active = 1 "
            "AND deadline < (SELECT AVG(deadline) FROM cases WHERE is_active = 1) "
            "ORDER BY deadline");
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::vector<std::string>> rows;
        while (rs->next()) {
            rows.push_back({
                cell(*rs, "case_id"),
                rs->getString("title"),
                cell(*rs, "deadline"),
                rs->getString("status")
            });
        }
        printRows(ui_, {"ID", "Title", "Deadline", "Status"}, rows);
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Analysis::decisionMenu() {
    bool back = false;
    while (!back) {
        const int choice = ui_.showMenu("E. DECISION SUPPORT", {
            "Workload score and recommend next investigator",
            "Integrity verification rate",
            "Days remaining vs deadline (negative = overdue)",
            "Back"
        });
        switch (choice) {
            case 1: decisionWorkload(); break;
            case 2: decisionIntegrity(); break;
            case 3: decisionDaysRemaining(); break;
            default: back = true; break;
        }
    }
}

void Analysis::decisionWorkload() {
    ui_.printHeader("DECISION SUPPORT: workload");
    try {
        // REQUIREMENT: DECISION SUPPORT
        std::cout << "Workload score = SUM of open/in-progress cases weighted Low 1, Medium 2, High 3, Critical 5.\n";
        auto stmt = db_.prepare(
            "SELECT i.investigator_id, i.name, "
            "COUNT(c.case_id) AS open_cases, "
            "SUM(CASE c.priority "
            "  WHEN 'Low' THEN 1 "
            "  WHEN 'Medium' THEN 2 "
            "  WHEN 'High' THEN 3 "
            "  WHEN 'Critical' THEN 5 "
            "  ELSE 0 END) AS score "
            "FROM investigators i "
            "LEFT JOIN cases c ON c.assigned_investigator_id = i.investigator_id "
            "  AND c.is_active = 1 AND c.status IN ('Open', 'In Progress') "
            "GROUP BY i.investigator_id, i.name "
            "ORDER BY score ASC, i.name");
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::vector<std::string>> rows;
        std::string recommendName = "-";
        std::string recommendScore = "0";
        bool first = true;
        while (rs->next()) {
            const std::string score = cell(*rs, "score");
            rows.push_back({
                cell(*rs, "investigator_id"),
                rs->getString("name"),
                cell(*rs, "open_cases"),
                score.empty() || score == "-" ? "0" : score
            });
            if (first) {
                recommendName = rs->getString("name");
                recommendScore = score.empty() || score == "-" ? "0" : score;
                first = false;
            }
        }
        printRows(ui_, {"ID", "Name", "Open cases", "Score"}, rows);
        std::cout << "Recommend next investigator: " << recommendName
                  << " (lowest score " << recommendScore << ").\n";
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Analysis::decisionIntegrity() {
    ui_.printHeader("DECISION SUPPORT: integrity rate");
    try {
        // REQUIREMENT: DECISION SUPPORT
        std::cout << "Integrity rate = verified evidence / total active evidence * 100.\n";
        auto stmt = db_.prepare(
            "SELECT COUNT(*) AS total_evidence, "
            "SUM(CASE WHEN verified = 1 THEN 1 ELSE 0 END) AS verified_count, "
            "SUM(CASE WHEN verified = 0 THEN 1 ELSE 0 END) AS unverified_count, "
            "ROUND("
            "  SUM(CASE WHEN verified = 1 THEN 1 ELSE 0 END) * 100.0 "
            "  / NULLIF(COUNT(*), 0), 2) AS verification_rate_pct "
            "FROM evidence WHERE is_active = 1");
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::vector<std::string>> rows;
        if (rs->next()) {
            rows.push_back({
                cell(*rs, "total_evidence"),
                cell(*rs, "verified_count"),
                cell(*rs, "unverified_count"),
                cell(*rs, "verification_rate_pct")
            });
        }
        printRows(ui_, {"Total", "Verified", "Unverified", "Rate %"}, rows);
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Analysis::decisionDaysRemaining() {
    ui_.printHeader("DECISION SUPPORT: days remaining");
    try {
        // REQUIREMENT: DECISION SUPPORT
        std::cout << "Days remaining = deadline - today; a negative value means the case is overdue.\n";
        auto stmt = db_.prepare(
            "SELECT case_id, title, deadline, "
            "CASE "
            "  WHEN DATEDIFF(deadline, CURDATE()) < 0 "
            "  THEN -ABS(DATEDIFF(deadline, CURDATE())) "
            "  ELSE DATEDIFF(deadline, CURDATE()) "
            "END AS days_remaining, "
            "CASE "
            "  WHEN DATEDIFF(deadline, CURDATE()) < 0 THEN 'overdue' "
            "  WHEN DATEDIFF(deadline, CURDATE()) = 0 THEN 'due today' "
            "  ELSE 'on track' "
            "END AS flag "
            "FROM cases "
            "WHERE is_active = 1 AND status <> 'Closed' "
            "ORDER BY days_remaining");
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::vector<std::string>> rows;
        while (rs->next()) {
            rows.push_back({
                cell(*rs, "case_id"),
                rs->getString("title"),
                cell(*rs, "deadline"),
                cell(*rs, "days_remaining"),
                rs->getString("flag")
            });
        }
        printRows(ui_, {"ID", "Title", "Deadline", "Days", "Flag"}, rows);
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}
