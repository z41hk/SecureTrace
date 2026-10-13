#include "Cases.h"
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
    "Fraud", "Data Breach", "Insider Threat", "Malware", "Other"};
const std::vector<std::string> kPriorities = {"Low", "Medium", "High", "Critical"};
const std::vector<std::string> kStatuses = {"Open", "In Progress", "Closed"};

void fail(ConsoleUI& ui, const std::exception& ex) {
    Logger::instance().error(std::string("cases: ") + ex.what());
    ui.printError("Operation failed.");
}
}

Cases::Cases(DatabaseConnection& db, AuditLog& audit, ConsoleUI& ui)
    : db_(db), audit_(audit), ui_(ui) {}

bool Cases::deny(const Session& session) {
    if (!session.authenticated ||
        (session.role != Role::Admin && session.role != Role::Investigator)) {
        ui_.printError("Access denied.");
        ui_.pause();
        return true;
    }
    return false;
}

void Cases::showMenu(const Session& session) {
    if (deny(session)) {
        return;
    }
    bool back = false;
    while (!back) {
        const int choice = ui_.showMenu("Cases", {
            "Create", "List all", "Read by ID", "Update", "Delete (soft)",
            "Search", "Restore inactive", "Back"
        });
        switch (choice) {
            case 1: create(session); break;
            case 2: listAll(); break;
            case 3: readOne(); break;
            case 4: update(session); break;
            case 5: remove(session); break;
            case 6: search(); break;
            case 7: restore(session); break;
            default: back = true; break;
        }
    }
}

bool Cases::investigatorExists(int investigatorId) {
    auto stmt = db_.prepare("SELECT investigator_id FROM investigators WHERE investigator_id = ?");
    stmt->setInt(1, investigatorId);
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    return rs->next();
}

int Cases::unverifiedCount(int caseId) {
    auto stmt = db_.prepare(
        "SELECT COUNT(*) AS n FROM evidence WHERE case_id = ? AND is_active = 1 AND verified = 0");
    stmt->setInt(1, caseId);
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    if (rs->next()) {
        return rs->getInt("n");
    }
    return 0;
}

void Cases::create(const Session& session) {
    ui_.printHeader("Create Case");
    try {
        const std::string title = ui_.readString("Title: ", 3, 150);
        const std::string type = kTypes[static_cast<std::size_t>(ui_.pick("Type: ", kTypes) - 1)];
        const std::string priority =
            kPriorities[static_cast<std::size_t>(ui_.pick("Priority: ", kPriorities) - 1)];
        const std::string opened = ui_.readDate("Opened date (YYYY-MM-DD): ", false);
        const std::string deadline = ui_.readDate("Deadline (YYYY-MM-DD): ", false);
        if (deadline < opened) {
            ui_.printError("Deadline cannot be before opened date.");
            ui_.pause();
            return;
        }

        auto list = db_.prepare(
            "SELECT investigator_id, name, specialty FROM investigators ORDER BY investigator_id");
        std::unique_ptr<sql::ResultSet> rs(list->executeQuery());
        std::vector<std::vector<std::string>> rows;
        while (rs->next()) {
            rows.push_back({
                std::to_string(rs->getInt("investigator_id")),
                rs->getString("name"),
                rs->getString("specialty")
            });
        }
        if (rows.empty()) {
            ui_.printError("No investigators exist. Add one first.");
            ui_.pause();
            return;
        }
        ui_.printTable({"ID", "Name", "Specialty"}, rows);
        const int investigatorId = ui_.readInt("Assigned investigator ID: ", 1, 1000000);
        if (!investigatorExists(investigatorId)) {
            ui_.printError("Investigator not found.");
            ui_.pause();
            return;
        }

        auto stmt = db_.prepare(
            "INSERT INTO cases (title, type, priority, status, opened_date, deadline, "
            "closed_date, assigned_investigator_id, is_active) "
            "VALUES (?, ?, ?, 'Open', ?, ?, NULL, ?, 1)");
        stmt->setString(1, title);
        stmt->setString(2, type);
        stmt->setString(3, priority);
        stmt->setString(4, opened);
        stmt->setString(5, deadline);
        stmt->setInt(6, investigatorId);
        stmt->executeUpdate();
        audit_.write(session.userId, "CREATE", "case title=" + title, true);
        ui_.printSuccess("Case created.");
    } catch (const std::exception& ex) {
        fail(ui_, ex);
        audit_.write(session.userId, "CREATE", "case", false);
    }
    ui_.pause();
}

void Cases::listAll() {
    ui_.printHeader("All Active Cases");
    std::cout << "Optional filter/sort. Leave blank to skip. Type c to cancel a field.\n";
    const std::string status = ui_.readOptional("Filter status: ");
    const std::string type = ui_.readOptional("Filter type: ");
    const int sortPick = ui_.pick("Sort by: ", {"ID", "Title", "Opened date", "Deadline", "Priority"});
    std::string orderBy = "c.case_id";
    if (sortPick == 2) orderBy = "c.title";
    else if (sortPick == 3) orderBy = "c.opened_date";
    else if (sortPick == 4) orderBy = "c.deadline";
    else if (sortPick == 5) {
        orderBy = "FIELD(c.priority, 'Critical', 'High', 'Medium', 'Low')";
    }
    try {
        auto stmt = db_.prepare(
            "SELECT c.case_id, c.title, c.type, c.priority, c.status, c.opened_date, "
            "c.deadline, i.name AS investigator "
            "FROM cases c JOIN investigators i ON c.assigned_investigator_id = i.investigator_id "
            "WHERE c.is_active = 1 "
            "AND (? = '' OR c.status = ?) "
            "AND (? = '' OR c.type = ?) "
            "ORDER BY " + orderBy);
        stmt->setString(1, status);
        stmt->setString(2, status);
        stmt->setString(3, type);
        stmt->setString(4, type);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::vector<std::string>> rows;
        while (rs->next()) {
            rows.push_back({
                std::to_string(rs->getInt("case_id")),
                rs->getString("title"),
                rs->getString("type"),
                rs->getString("priority"),
                rs->getString("status"),
                rs->getString("opened_date"),
                rs->getString("deadline"),
                rs->getString("investigator")
            });
        }
        if (rows.empty()) {
            std::cout << "No cases found.\n";
        } else {
            ui_.printTable({"ID", "Title", "Type", "Priority", "Status", "Opened", "Deadline", "Investigator"}, rows);
        }
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Cases::readOne() {
    ui_.printHeader("Read Case");
    const int id = ui_.readInt("Case ID: ", 1, 1000000);
    try {
        auto stmt = db_.prepare(
            "SELECT c.case_id, c.title, c.type, c.priority, c.status, c.opened_date, "
            "c.deadline, c.closed_date, c.is_active, i.name AS investigator "
            "FROM cases c JOIN investigators i ON c.assigned_investigator_id = i.investigator_id "
            "WHERE c.case_id = ?");
        stmt->setInt(1, id);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        if (!rs->next()) {
            ui_.printError("Case not found.");
            ui_.pause();
            return;
        }
        std::vector<std::vector<std::string>> rows = {{
            std::to_string(rs->getInt("case_id")),
            rs->getString("title"),
            rs->getString("type"),
            rs->getString("priority"),
            rs->getString("status"),
            rs->getString("opened_date"),
            rs->getString("deadline"),
            rs->isNull("closed_date") ? "-" : rs->getString("closed_date"),
            rs->getString("investigator"),
            rs->getBoolean("is_active") ? "yes" : "no"
        }};
        ui_.printTable({"ID", "Title", "Type", "Priority", "Status", "Opened", "Deadline", "Closed", "Investigator", "Active"}, rows);
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Cases::update(const Session& session) {
    ui_.printHeader("Update Case");
    const int id = ui_.readInt("Case ID: ", 1, 1000000);
    try {
        auto load = db_.prepare(
            "SELECT title, type, priority, status, opened_date, deadline, closed_date, "
            "assigned_investigator_id, is_active FROM cases WHERE case_id = ?");
        load->setInt(1, id);
        std::unique_ptr<sql::ResultSet> rs(load->executeQuery());
        if (!rs->next()) {
            ui_.printError("Case not found.");
            ui_.pause();
            return;
        }
        if (!rs->getBoolean("is_active")) {
            ui_.printError("Cannot update an inactive case.");
            ui_.pause();
            return;
        }

        const std::string oldTitle = rs->getString("title");
        const std::string oldType = rs->getString("type");
        const std::string oldPriority = rs->getString("priority");
        const std::string oldStatus = rs->getString("status");
        const std::string opened = rs->getString("opened_date");
        const std::string oldDeadline = rs->getString("deadline");
        const int oldInv = rs->getInt("assigned_investigator_id");

        std::cout << "Leave a field blank to keep the current value.\n";
        std::cout << "Current title: " << oldTitle << "\n";
        std::string title = ui_.readOptional("New title: ");
        if (title.empty()) title = oldTitle;

        std::cout << "Current type: " << oldType << "\n";
        const std::string type = ui_.confirm("Change type?")
            ? kTypes[static_cast<std::size_t>(ui_.pick("Type: ", kTypes) - 1)]
            : oldType;

        std::cout << "Current priority: " << oldPriority << "\n";
        const std::string priority = ui_.confirm("Change priority?")
            ? kPriorities[static_cast<std::size_t>(ui_.pick("Priority: ", kPriorities) - 1)]
            : oldPriority;

        std::cout << "Current deadline: " << oldDeadline << "\n";
        std::string deadline = ui_.readDate("New deadline (blank = keep): ", true);
        if (deadline.empty()) deadline = oldDeadline;
        if (deadline < opened) {
            ui_.printError("Deadline cannot be before opened date.");
            ui_.pause();
            return;
        }

        std::cout << "Current investigator ID: " << oldInv << "\n";
        int investigatorId = ui_.readOptionalInt("New investigator ID (blank = keep): ", 1, 1000000);
        if (investigatorId < 0) investigatorId = oldInv;
        if (!investigatorExists(investigatorId)) {
            ui_.printError("Investigator not found.");
            ui_.pause();
            return;
        }

        std::cout << "Current status: " << oldStatus << "\n";
        std::string status = oldStatus;
        std::string closedDate;
        bool closing = false;
        if (ui_.confirm("Change status?")) {
            status = kStatuses[static_cast<std::size_t>(ui_.pick("Status: ", kStatuses) - 1)];
        }
        if (status == "Closed") {
            closing = true;
            const int pending = unverifiedCount(id);
            if (pending > 0) {
                ui_.printError("Case has " + std::to_string(pending) + " unverified evidence item(s).");
                if (session.role != Role::Admin) {
                    ui_.printError("Only an admin can override this.");
                    audit_.write(session.userId, "UPDATE", "case_id=" + std::to_string(id) + " close blocked", false);
                    ui_.pause();
                    return;
                }
                if (!ui_.confirm("Admin override and close anyway?")) {
                    audit_.write(session.userId, "UPDATE", "case_id=" + std::to_string(id) + " close cancelled", false);
                    ui_.pause();
                    return;
                }
                audit_.write(session.userId, "CASE_CLOSE_OVERRIDE",
                             "case_id=" + std::to_string(id) + " unverified=" + std::to_string(pending), true);
            }
            closedDate = ui_.readDate("Closed date (blank = today): ", true);
            if (!closedDate.empty() && closedDate < opened) {
                ui_.printError("Closed date cannot be before opened date.");
                ui_.pause();
                return;
            }
        }

        auto stmt = db_.prepare(
            "UPDATE cases SET title = ?, type = ?, priority = ?, status = ?, deadline = ?, "
            "assigned_investigator_id = ?, "
            "closed_date = CASE "
            "  WHEN ? = 'Closed' AND ? = '' THEN CURDATE() "
            "  WHEN ? = 'Closed' THEN ? "
            "  ELSE NULL END "
            "WHERE case_id = ? AND is_active = 1");
        stmt->setString(1, title);
        stmt->setString(2, type);
        stmt->setString(3, priority);
        stmt->setString(4, status);
        stmt->setString(5, deadline);
        stmt->setInt(6, investigatorId);
        stmt->setString(7, status);
        stmt->setString(8, closedDate);
        stmt->setString(9, status);
        stmt->setString(10, closedDate);
        stmt->setInt(11, id);
        const int changed = stmt->executeUpdate();
        if (changed == 0) {
            ui_.printError("Operation failed.");
            audit_.write(session.userId, "UPDATE", "case_id=" + std::to_string(id), false);
        } else {
            audit_.write(session.userId, "UPDATE",
                         "case_id=" + std::to_string(id) + (closing ? " closed" : ""), true);
            ui_.printSuccess("Case updated.");
        }
    } catch (const std::exception& ex) {
        fail(ui_, ex);
        audit_.write(session.userId, "UPDATE", "case", false);
    }
    ui_.pause();
}

void Cases::remove(const Session& session) {
    ui_.printHeader("Delete Case (soft)");
    const int id = ui_.readInt("Case ID: ", 1, 1000000);
    if (!ui_.confirm("Mark this case inactive?")) {
        ui_.pause();
        return;
    }
    try {
        auto stmt = db_.prepare("UPDATE cases SET is_active = 0 WHERE case_id = ? AND is_active = 1");
        stmt->setInt(1, id);
        const int changed = stmt->executeUpdate();
        if (changed == 0) {
            ui_.printError("Case not found or already inactive.");
            audit_.write(session.userId, "DELETE", "case_id=" + std::to_string(id), false);
        } else {
            audit_.write(session.userId, "DELETE", "case_id=" + std::to_string(id), true);
            ui_.printSuccess("Case marked inactive.");
        }
    } catch (const std::exception& ex) {
        fail(ui_, ex);
        audit_.write(session.userId, "DELETE", "case", false);
    }
    ui_.pause();
}

void Cases::restore(const Session& session) {
    ui_.printHeader("Restore Case");
    if (session.role != Role::Admin) {
        ui_.printError("Access denied.");
        ui_.pause();
        return;
    }
    const int id = ui_.readInt("Inactive case ID: ", 1, 1000000);
    if (!ui_.confirm("Restore this case?")) {
        ui_.pause();
        return;
    }
    try {
        auto stmt = db_.prepare("UPDATE cases SET is_active = 1 WHERE case_id = ? AND is_active = 0");
        stmt->setInt(1, id);
        const int changed = stmt->executeUpdate();
        if (changed == 0) {
            ui_.printError("Case not found or already active.");
            audit_.write(session.userId, "UPDATE", "restore case_id=" + std::to_string(id), false);
        } else {
            audit_.write(session.userId, "UPDATE", "restore case_id=" + std::to_string(id), true);
            ui_.printSuccess("Case restored.");
        }
    } catch (const std::exception& ex) {
        fail(ui_, ex);
        audit_.write(session.userId, "UPDATE", "restore case", false);
    }
    ui_.pause();
}

void Cases::search() {
    ui_.printHeader("Search Cases");
    std::cout << "Leave a field blank to skip it. Keyword matches title, type, status, or investigator.\n";
    const int id = ui_.readOptionalInt("Case ID: ", 1, 1000000);
    const std::string status = ui_.readOptional("Status (Open / In Progress / Closed): ");
    const std::string type = ui_.readOptional("Type: ");
    const std::string keyword = ui_.readOptional("Keyword: ");
    try {
        auto stmt = db_.prepare(
            "SELECT c.case_id, c.title, c.type, c.priority, c.status, c.is_active, i.name AS investigator "
            "FROM cases c JOIN investigators i ON c.assigned_investigator_id = i.investigator_id "
            "WHERE (? = 0 OR c.case_id = ?) "
            "AND (? = '' OR c.status = ?) "
            "AND (? = '' OR c.type = ?) "
            "AND (? = '' OR c.title LIKE CONCAT('%', ?, '%') "
            "          OR c.type LIKE CONCAT('%', ?, '%') "
            "          OR c.status LIKE CONCAT('%', ?, '%') "
            "          OR i.name LIKE CONCAT('%', ?, '%')) "
            "ORDER BY c.case_id");
        const int idValue = id < 0 ? 0 : id;
        stmt->setInt(1, idValue);
        stmt->setInt(2, idValue);
        stmt->setString(3, status);
        stmt->setString(4, status);
        stmt->setString(5, type);
        stmt->setString(6, type);
        stmt->setString(7, keyword);
        stmt->setString(8, keyword);
        stmt->setString(9, keyword);
        stmt->setString(10, keyword);
        stmt->setString(11, keyword);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::vector<std::string>> rows;
        while (rs->next()) {
            rows.push_back({
                std::to_string(rs->getInt("case_id")),
                rs->getString("title"),
                rs->getString("type"),
                rs->getString("priority"),
                rs->getString("status"),
                rs->getBoolean("is_active") ? "yes" : "no",
                rs->getString("investigator")
            });
        }
        if (rows.empty()) {
            std::cout << "No matches.\n";
        } else {
            ui_.printTable({"ID", "Title", "Type", "Priority", "Status", "Active", "Investigator"}, rows);
        }
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}
