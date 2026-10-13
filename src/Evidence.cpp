#include "Evidence.h"
#include "HashUtil.h"
#include "Logger.h"

#include <cppconn/exception.h>
#include <cppconn/resultset.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

namespace {
const std::vector<std::string> kTypes = {
    "Laptop", "USB", "Phone", "Disk Image", "Log File", "Other"};

void fail(ConsoleUI& ui, const std::exception& ex) {
    Logger::instance().error(std::string("evidence: ") + ex.what());
    ui.printError("Operation failed.");
}

std::string formatSize(double sizeMb) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << sizeMb;
    return out.str();
}

std::string formatSize(sql::ResultSet& rs, const std::string& col) {
    return rs.getString(col);
}

std::string shortHash(const std::string& hash) {
    return hash.size() > 16 ? hash.substr(0, 16) + "..." : hash;
}
}

Evidence::Evidence(DatabaseConnection& db, AuditLog& audit, ConsoleUI& ui)
    : db_(db), audit_(audit), ui_(ui) {}

bool Evidence::deny(const Session& session) {
    if (!session.authenticated ||
        (session.role != Role::Admin && session.role != Role::Investigator)) {
        ui_.printError("Access denied.");
        ui_.pause();
        return true;
    }
    return false;
}

void Evidence::showMenu(const Session& session) {
    if (deny(session)) {
        return;
    }
    bool back = false;
    while (!back) {
        const int choice = ui_.showMenu("Evidence", {
            "Create", "List all", "Read by ID", "Update", "Delete (soft)",
            "Search", "Verify integrity", "Restore inactive", "Back"
        });
        switch (choice) {
            case 1: create(session); break;
            case 2: listAll(); break;
            case 3: readOne(); break;
            case 4: update(session); break;
            case 5: remove(session); break;
            case 6: search(); break;
            case 7: verify(session); break;
            case 8: restore(session); break;
            default: back = true; break;
        }
    }
}

bool Evidence::caseIsActive(int caseId) {
    auto stmt = db_.prepare("SELECT is_active FROM cases WHERE case_id = ?");
    stmt->setInt(1, caseId);
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    return rs->next() && rs->getBoolean("is_active");
}

std::pair<std::string, double> Evidence::hashFromUser() {
    const int source = ui_.pick("Source: ", {"File path", "Text content"});
    double sizeMb = 0.0;
    std::string hash;
    if (source == 1) {
        const std::string path = ui_.readString("File path: ", 1, 260);
        hash = HashUtil::sha256File(path, sizeMb);
        if (hash.empty()) {
            ui_.printError("Could not read that file.");
        }
    } else {
        const std::string text = ui_.readString("Text content: ", 1, 4096);
        hash = HashUtil::sha256Text(text, sizeMb);
    }
    return {hash, sizeMb};
}

void Evidence::create(const Session& session) {
    ui_.printHeader("Create Evidence");
    try {
        const int caseId = ui_.readInt("Case ID: ", 1, 1000000);
        if (!caseIsActive(caseId)) {
            ui_.printError("Case not found or inactive.");
            ui_.pause();
            return;
        }
        const std::string description = ui_.readString("Description: ", 3, 255);
        const std::string type = kTypes[static_cast<std::size_t>(ui_.pick("Type: ", kTypes) - 1)];
        const auto hashed = hashFromUser();
        if (hashed.first.empty()) {
            audit_.write(session.userId, "CREATE", "evidence", false);
            ui_.pause();
            return;
        }
        if (hashed.second <= 0.0) {
            ui_.printError("Size cannot be negative or zero.");
            ui_.pause();
            return;
        }
        const std::string collected = ui_.readDate("Collected date (YYYY-MM-DD): ", false);
        const std::string location = ui_.readString("Storage location: ", 2, 120);

        auto stmt = db_.prepare(
            "INSERT INTO evidence (case_id, description, type, sha256_hash, size_mb, "
            "collected_date, storage_location, verified, is_active) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, 0, 1)");
        stmt->setInt(1, caseId);
        stmt->setString(2, description);
        stmt->setString(3, type);
        stmt->setString(4, hashed.first);
        stmt->setDouble(5, hashed.second);
        stmt->setString(6, collected);
        stmt->setString(7, location);
        stmt->executeUpdate();
        audit_.write(session.userId, "CREATE", "evidence case_id=" + std::to_string(caseId), true);
        ui_.printSuccess("Evidence created. SHA-256: " + hashed.first);
        ui_.printSuccess("Size MB: " + formatSize(hashed.second));
    } catch (const std::exception& ex) {
        fail(ui_, ex);
        audit_.write(session.userId, "CREATE", "evidence", false);
    }
    ui_.pause();
}

void Evidence::listAll() {
    ui_.printHeader("All Active Evidence");
    std::cout << "Optional filter/sort. Leave blank to skip.\n";
    const std::string type = ui_.readOptional("Filter type: ");
    const std::string verified = ui_.readOptional("Filter verified (yes/no): ");
    const int sortPick = ui_.pick("Sort by: ", {"ID", "Case ID", "Type", "Size", "Collected date"});
    std::string orderBy = "evidence_id";
    if (sortPick == 2) orderBy = "case_id";
    else if (sortPick == 3) orderBy = "type";
    else if (sortPick == 4) orderBy = "size_mb";
    else if (sortPick == 5) orderBy = "collected_date";
    try {
        int verifiedFlag = -1;
        if (verified == "yes" || verified == "Y" || verified == "1") verifiedFlag = 1;
        else if (verified == "no" || verified == "N" || verified == "0") verifiedFlag = 0;
        auto stmt = db_.prepare(
            "SELECT evidence_id, case_id, description, type, sha256_hash, size_mb, "
            "verified FROM evidence WHERE is_active = 1 "
            "AND (? = '' OR type = ?) "
            "AND (? < 0 OR verified = ?) "
            "ORDER BY " + orderBy);
        stmt->setString(1, type);
        stmt->setString(2, type);
        stmt->setInt(3, verifiedFlag);
        stmt->setInt(4, verifiedFlag < 0 ? 0 : verifiedFlag);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::vector<std::string>> rows;
        while (rs->next()) {
            rows.push_back({
                std::to_string(rs->getInt("evidence_id")),
                std::to_string(rs->getInt("case_id")),
                rs->getString("description"),
                rs->getString("type"),
                shortHash(rs->getString("sha256_hash")),
                formatSize(*rs, "size_mb"),
                rs->getBoolean("verified") ? "yes" : "no"
            });
        }
        if (rows.empty()) {
            std::cout << "No evidence found.\n";
        } else {
            ui_.printTable({"ID", "Case", "Description", "Type", "SHA-256", "Size MB", "Verified"}, rows);
        }
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Evidence::readOne() {
    ui_.printHeader("Read Evidence");
    const int id = ui_.readInt("Evidence ID: ", 1, 1000000);
    try {
        auto stmt = db_.prepare(
            "SELECT evidence_id, case_id, description, type, sha256_hash, size_mb, "
            "collected_date, storage_location, verified, is_active "
            "FROM evidence WHERE evidence_id = ?");
        stmt->setInt(1, id);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        if (!rs->next()) {
            ui_.printError("Evidence not found.");
            ui_.pause();
            return;
        }
        std::vector<std::vector<std::string>> rows = {{
            std::to_string(rs->getInt("evidence_id")),
            std::to_string(rs->getInt("case_id")),
            rs->getString("description"),
            rs->getString("type"),
            rs->getString("sha256_hash"),
            formatSize(*rs, "size_mb"),
            rs->getString("collected_date"),
            rs->getString("storage_location"),
            rs->getBoolean("verified") ? "yes" : "no",
            rs->getBoolean("is_active") ? "yes" : "no"
        }};
        ui_.printTable({"ID", "Case", "Description", "Type", "SHA-256", "Size MB",
                        "Collected", "Location", "Verified", "Active"}, rows);
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Evidence::update(const Session& session) {
    ui_.printHeader("Update Evidence");
    const int id = ui_.readInt("Evidence ID: ", 1, 1000000);
    try {
        auto load = db_.prepare(
            "SELECT description, type, storage_location, collected_date, is_active "
            "FROM evidence WHERE evidence_id = ?");
        load->setInt(1, id);
        std::unique_ptr<sql::ResultSet> rs(load->executeQuery());
        if (!rs->next()) {
            ui_.printError("Evidence not found.");
            ui_.pause();
            return;
        }
        if (!rs->getBoolean("is_active")) {
            ui_.printError("Cannot update inactive evidence.");
            ui_.pause();
            return;
        }
        const std::string oldDesc = rs->getString("description");
        const std::string oldType = rs->getString("type");
        const std::string oldLoc = rs->getString("storage_location");
        const std::string oldDate = rs->getString("collected_date");

        std::cout << "Leave blank to keep the current value. Hash cannot be edited here.\n";
        std::cout << "Current description: " << oldDesc << "\n";
        std::string description = ui_.readOptional("New description: ");
        if (description.empty()) description = oldDesc;

        std::cout << "Current type: " << oldType << "\n";
        const std::string type = ui_.confirm("Change type?")
            ? kTypes[static_cast<std::size_t>(ui_.pick("Type: ", kTypes) - 1)]
            : oldType;

        std::cout << "Current location: " << oldLoc << "\n";
        std::string location = ui_.readOptional("New storage location: ");
        if (location.empty()) location = oldLoc;

        std::cout << "Current collected date: " << oldDate << "\n";
        std::string collected = ui_.readDate("New collected date (blank = keep): ", true);
        if (collected.empty()) collected = oldDate;

        auto stmt = db_.prepare(
            "UPDATE evidence SET description = ?, type = ?, storage_location = ?, collected_date = ? "
            "WHERE evidence_id = ? AND is_active = 1");
        stmt->setString(1, description);
        stmt->setString(2, type);
        stmt->setString(3, location);
        stmt->setString(4, collected);
        stmt->setInt(5, id);
        stmt->executeUpdate();
        audit_.write(session.userId, "UPDATE", "evidence_id=" + std::to_string(id), true);
        ui_.printSuccess("Evidence updated.");
    } catch (const std::exception& ex) {
        fail(ui_, ex);
        audit_.write(session.userId, "UPDATE", "evidence", false);
    }
    ui_.pause();
}

void Evidence::remove(const Session& session) {
    ui_.printHeader("Delete Evidence (soft)");
    const int id = ui_.readInt("Evidence ID: ", 1, 1000000);
    if (!ui_.confirm("Mark this evidence inactive?")) {
        ui_.pause();
        return;
    }
    try {
        auto stmt = db_.prepare(
            "UPDATE evidence SET is_active = 0 WHERE evidence_id = ? AND is_active = 1");
        stmt->setInt(1, id);
        const int changed = stmt->executeUpdate();
        if (changed == 0) {
            ui_.printError("Evidence not found or already inactive.");
            audit_.write(session.userId, "DELETE", "evidence_id=" + std::to_string(id), false);
        } else {
            audit_.write(session.userId, "DELETE", "evidence_id=" + std::to_string(id), true);
            ui_.printSuccess("Evidence marked inactive.");
        }
    } catch (const std::exception& ex) {
        fail(ui_, ex);
        audit_.write(session.userId, "DELETE", "evidence", false);
    }
    ui_.pause();
}

void Evidence::restore(const Session& session) {
    ui_.printHeader("Restore Evidence");
    if (session.role != Role::Admin) {
        ui_.printError("Access denied.");
        ui_.pause();
        return;
    }
    const int id = ui_.readInt("Inactive evidence ID: ", 1, 1000000);
    if (!ui_.confirm("Restore this evidence?")) {
        ui_.pause();
        return;
    }
    try {
        auto stmt = db_.prepare(
            "UPDATE evidence SET is_active = 1 WHERE evidence_id = ? AND is_active = 0");
        stmt->setInt(1, id);
        const int changed = stmt->executeUpdate();
        if (changed == 0) {
            ui_.printError("Evidence not found or already active.");
            audit_.write(session.userId, "UPDATE", "restore evidence_id=" + std::to_string(id), false);
        } else {
            audit_.write(session.userId, "UPDATE", "restore evidence_id=" + std::to_string(id), true);
            ui_.printSuccess("Evidence restored.");
        }
    } catch (const std::exception& ex) {
        fail(ui_, ex);
        audit_.write(session.userId, "UPDATE", "restore evidence", false);
    }
    ui_.pause();
}

void Evidence::search() {
    ui_.printHeader("Search Evidence");
    std::cout << "Leave a field blank to skip it. Keyword matches description, type, hash, or location.\n";
    const int id = ui_.readOptionalInt("Evidence ID: ", 1, 1000000);
    const int caseId = ui_.readOptionalInt("Case ID: ", 1, 1000000);
    const std::string type = ui_.readOptional("Type: ");
    const std::string hash = ui_.readOptional("SHA-256 (full or prefix): ");
    const std::string keyword = ui_.readOptional("Keyword: ");
    try {
        auto stmt = db_.prepare(
            "SELECT evidence_id, case_id, description, type, sha256_hash, verified, is_active "
            "FROM evidence "
            "WHERE (? = 0 OR evidence_id = ?) "
            "AND (? = 0 OR case_id = ?) "
            "AND (? = '' OR type = ?) "
            "AND (? = '' OR sha256_hash LIKE CONCAT(?, '%')) "
            "AND (? = '' OR description LIKE CONCAT('%', ?, '%') "
            "          OR type LIKE CONCAT('%', ?, '%') "
            "          OR sha256_hash LIKE CONCAT('%', ?, '%') "
            "          OR storage_location LIKE CONCAT('%', ?, '%')) "
            "ORDER BY evidence_id");
        const int idValue = id < 0 ? 0 : id;
        const int caseValue = caseId < 0 ? 0 : caseId;
        stmt->setInt(1, idValue);
        stmt->setInt(2, idValue);
        stmt->setInt(3, caseValue);
        stmt->setInt(4, caseValue);
        stmt->setString(5, type);
        stmt->setString(6, type);
        stmt->setString(7, hash);
        stmt->setString(8, hash);
        stmt->setString(9, keyword);
        stmt->setString(10, keyword);
        stmt->setString(11, keyword);
        stmt->setString(12, keyword);
        stmt->setString(13, keyword);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::vector<std::string>> rows;
        while (rs->next()) {
            rows.push_back({
                std::to_string(rs->getInt("evidence_id")),
                std::to_string(rs->getInt("case_id")),
                rs->getString("description"),
                rs->getString("type"),
                shortHash(rs->getString("sha256_hash")),
                rs->getBoolean("verified") ? "yes" : "no",
                rs->getBoolean("is_active") ? "yes" : "no"
            });
        }
        if (rows.empty()) {
            std::cout << "No matches.\n";
        } else {
            ui_.printTable({"ID", "Case", "Description", "Type", "SHA-256", "Verified", "Active"}, rows);
        }
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Evidence::verify(const Session& session) {
    ui_.printHeader("Verify Integrity");
    const int id = ui_.readInt("Evidence ID: ", 1, 1000000);
    try {
        auto load = db_.prepare(
            "SELECT sha256_hash, is_active FROM evidence WHERE evidence_id = ?");
        load->setInt(1, id);
        std::unique_ptr<sql::ResultSet> rs(load->executeQuery());
        if (!rs->next()) {
            ui_.printError("Evidence not found.");
            ui_.pause();
            return;
        }
        if (!rs->getBoolean("is_active")) {
            ui_.printError("Cannot verify inactive evidence.");
            ui_.pause();
            return;
        }
        const std::string stored = rs->getString("sha256_hash");
        std::cout << "Provide the same source used at collection.\n";
        const auto hashed = hashFromUser();
        if (hashed.first.empty()) {
            audit_.write(session.userId, "HASH_VERIFY", "evidence_id=" + std::to_string(id), false);
            ui_.pause();
            return;
        }
        const bool match = (hashed.first == stored);
        auto stmt = db_.prepare("UPDATE evidence SET verified = ? WHERE evidence_id = ?");
        stmt->setInt(1, match ? 1 : 0);
        stmt->setInt(2, id);
        stmt->executeUpdate();
        audit_.write(session.userId, "HASH_VERIFY",
                     "evidence_id=" + std::to_string(id) + (match ? " match" : " mismatch"), match);
        if (match) {
            ui_.printSuccess("Hash matches. Evidence marked verified.");
        } else {
            ui_.printError("Hash mismatch. Evidence marked unverified.");
            std::cout << "Stored : " << stored << "\n";
            std::cout << "Computed: " << hashed.first << "\n";
        }
    } catch (const std::exception& ex) {
        fail(ui_, ex);
        audit_.write(session.userId, "HASH_VERIFY", "evidence_id=" + std::to_string(id), false);
    }
    ui_.pause();
}
