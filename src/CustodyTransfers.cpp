#include "CustodyTransfers.h"
#include "Logger.h"

#include <cppconn/exception.h>
#include <cppconn/resultset.h>
#include <iostream>
#include <memory>
#include <vector>

namespace {
void fail(ConsoleUI& ui, const std::exception& ex) {
    Logger::instance().error(std::string("custody: ") + ex.what());
    ui.printError("Operation failed.");
}
}

CustodyTransfers::CustodyTransfers(DatabaseConnection& db, AuditLog& audit, ConsoleUI& ui)
    : db_(db), audit_(audit), ui_(ui) {}

bool CustodyTransfers::deny(const Session& session) {
    if (!session.authenticated ||
        (session.role != Role::Admin && session.role != Role::Investigator)) {
        ui_.printError("Access denied.");
        ui_.pause();
        return true;
    }
    return false;
}

void CustodyTransfers::showMenu(const Session& session) {
    if (deny(session)) {
        return;
    }
    bool back = false;
    while (!back) {
        // Chain of custody is append-only. Once a transfer row is created it
        // must not be silently edited, so this module exposes Create, Read
        // and Search only. Update and Delete are omitted on purpose.
        const int choice = ui_.showMenu("Custody Transfers", {
            "Create", "List all", "Read by ID", "Search", "Back"
        });
        switch (choice) {
            case 1: create(session); break;
            case 2: listAll(); break;
            case 3: readOne(); break;
            case 4: search(); break;
            default: back = true; break;
        }
    }
}

bool CustodyTransfers::evidenceIsActive(int evidenceId) {
    auto stmt = db_.prepare("SELECT is_active FROM evidence WHERE evidence_id = ?");
    stmt->setInt(1, evidenceId);
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    return rs->next() && rs->getBoolean("is_active");
}

void CustodyTransfers::create(const Session& session) {
    ui_.printHeader("Create Custody Transfer");
    try {
        const int evidenceId = ui_.readInt("Evidence ID: ", 1, 1000000);
        if (!evidenceIsActive(evidenceId)) {
            ui_.printError("Cannot add a transfer for missing or inactive evidence.");
            audit_.write(session.userId, "CREATE", "custody evidence_id=" + std::to_string(evidenceId), false);
            ui_.pause();
            return;
        }
        const std::string fromPerson = ui_.readString("From person: ", 2, 100);
        const std::string toPerson = ui_.readString("To person: ", 2, 100);
        const std::string when = ui_.readDateTime("Datetime YYYY-MM-DD HH:MM:SS (blank = now): ", true);
        const std::string reason = ui_.readString("Reason: ", 3, 255);

        auto stmt = db_.prepare(
            "INSERT INTO custody_transfers (evidence_id, from_person, to_person, transfer_datetime, reason) "
            "VALUES (?, ?, ?, IF(? = '', NOW(), ?), ?)");
        stmt->setInt(1, evidenceId);
        stmt->setString(2, fromPerson);
        stmt->setString(3, toPerson);
        stmt->setString(4, when);
        stmt->setString(5, when);
        stmt->setString(6, reason);
        stmt->executeUpdate();
        audit_.write(session.userId, "CREATE", "custody evidence_id=" + std::to_string(evidenceId), true);
        ui_.printSuccess("Transfer recorded.");
    } catch (const std::exception& ex) {
        fail(ui_, ex);
        audit_.write(session.userId, "CREATE", "custody", false);
    }
    ui_.pause();
}

void CustodyTransfers::listAll() {
    ui_.printHeader("All Custody Transfers");
    try {
        auto stmt = db_.prepare(
            "SELECT transfer_id, evidence_id, from_person, to_person, transfer_datetime, reason "
            "FROM custody_transfers ORDER BY transfer_id");
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::vector<std::string>> rows;
        while (rs->next()) {
            rows.push_back({
                std::to_string(rs->getInt("transfer_id")),
                std::to_string(rs->getInt("evidence_id")),
                rs->getString("from_person"),
                rs->getString("to_person"),
                rs->getString("transfer_datetime"),
                rs->getString("reason")
            });
        }
        if (rows.empty()) {
            std::cout << "No transfers found.\n";
        } else {
            ui_.printTable({"ID", "Evidence", "From", "To", "When", "Reason"}, rows);
        }
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void CustodyTransfers::readOne() {
    ui_.printHeader("Read Custody Transfer");
    const int id = ui_.readInt("Transfer ID: ", 1, 1000000);
    try {
        auto stmt = db_.prepare(
            "SELECT transfer_id, evidence_id, from_person, to_person, transfer_datetime, reason "
            "FROM custody_transfers WHERE transfer_id = ?");
        stmt->setInt(1, id);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        if (!rs->next()) {
            ui_.printError("Transfer not found.");
            ui_.pause();
            return;
        }
        std::vector<std::vector<std::string>> rows = {{
            std::to_string(rs->getInt("transfer_id")),
            std::to_string(rs->getInt("evidence_id")),
            rs->getString("from_person"),
            rs->getString("to_person"),
            rs->getString("transfer_datetime"),
            rs->getString("reason")
        }};
        ui_.printTable({"ID", "Evidence", "From", "To", "When", "Reason"}, rows);
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void CustodyTransfers::search() {
    ui_.printHeader("Search Custody Transfers");
    std::cout << "Leave a field blank to skip it.\n";
    const int id = ui_.readOptionalInt("Transfer ID: ", 1, 1000000);
    const int evidenceId = ui_.readOptionalInt("Evidence ID: ", 1, 1000000);
    const std::string fromPerson = ui_.readOptional("From person contains: ");
    const std::string toPerson = ui_.readOptional("To person contains: ");
    try {
        auto stmt = db_.prepare(
            "SELECT transfer_id, evidence_id, from_person, to_person, transfer_datetime, reason "
            "FROM custody_transfers "
            "WHERE (? = 0 OR transfer_id = ?) "
            "AND (? = 0 OR evidence_id = ?) "
            "AND (? = '' OR from_person LIKE CONCAT('%', ?, '%')) "
            "AND (? = '' OR to_person LIKE CONCAT('%', ?, '%')) "
            "ORDER BY transfer_id");
        const int idValue = id < 0 ? 0 : id;
        const int evidenceValue = evidenceId < 0 ? 0 : evidenceId;
        stmt->setInt(1, idValue);
        stmt->setInt(2, idValue);
        stmt->setInt(3, evidenceValue);
        stmt->setInt(4, evidenceValue);
        stmt->setString(5, fromPerson);
        stmt->setString(6, fromPerson);
        stmt->setString(7, toPerson);
        stmt->setString(8, toPerson);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::vector<std::string>> rows;
        while (rs->next()) {
            rows.push_back({
                std::to_string(rs->getInt("transfer_id")),
                std::to_string(rs->getInt("evidence_id")),
                rs->getString("from_person"),
                rs->getString("to_person"),
                rs->getString("transfer_datetime"),
                rs->getString("reason")
            });
        }
        if (rows.empty()) {
            std::cout << "No matches.\n";
        } else {
            ui_.printTable({"ID", "Evidence", "From", "To", "When", "Reason"}, rows);
        }
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}
