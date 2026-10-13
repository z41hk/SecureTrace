#include "Investigators.h"
#include "Logger.h"

#include <cppconn/exception.h>
#include <cppconn/resultset.h>
#include <iostream>
#include <memory>
#include <vector>

namespace {
void fail(ConsoleUI& ui, const std::exception& ex) {
    Logger::instance().error(std::string("investigators: ") + ex.what());
    ui.printError("Operation failed.");
}
}

Investigators::Investigators(DatabaseConnection& db, AuditLog& audit, ConsoleUI& ui)
    : db_(db), audit_(audit), ui_(ui) {}

bool Investigators::deny(const Session& session) {
    if (!session.authenticated || session.role != Role::Admin) {
        ui_.printError("Access denied.");
        ui_.pause();
        return true;
    }
    return false;
}

void Investigators::showMenu(const Session& session) {
    if (deny(session)) {
        return;
    }
    bool back = false;
    while (!back) {
        const int choice = ui_.showMenu("Investigators", {
            "Create", "List all", "Read by ID", "Update", "Delete", "Search", "Back"
        });
        switch (choice) {
            case 1: create(session); break;
            case 2: listAll(); break;
            case 3: readOne(); break;
            case 4: update(session); break;
            case 5: remove(session); break;
            case 6: search(); break;
            default: back = true; break;
        }
    }
}

void Investigators::create(const Session& session) {
    ui_.printHeader("Create Investigator");
    try {
        const std::string name = ui_.readString("Name: ", 2, 100);
        const std::string role = ui_.readString("Role: ", 2, 80);
        const std::string specialty = ui_.readString("Specialty: ", 2, 80);
        const std::string department = ui_.readString("Department: ", 2, 80);
        auto stmt = db_.prepare(
            "INSERT INTO investigators (name, role, specialty, department) VALUES (?, ?, ?, ?)");
        stmt->setString(1, name);
        stmt->setString(2, role);
        stmt->setString(3, specialty);
        stmt->setString(4, department);
        stmt->executeUpdate();
        audit_.write(session.userId, "CREATE", "investigator name=" + name, true);
        ui_.printSuccess("Investigator created.");
    } catch (const std::exception& ex) {
        fail(ui_, ex);
        audit_.write(session.userId, "CREATE", "investigator", false);
    }
    ui_.pause();
}

void Investigators::listAll() {
    ui_.printHeader("All Investigators");
    try {
        auto stmt = db_.prepare(
            "SELECT investigator_id, name, role, specialty, department "
            "FROM investigators ORDER BY investigator_id");
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::vector<std::string>> rows;
        while (rs->next()) {
            rows.push_back({
                std::to_string(rs->getInt("investigator_id")),
                rs->getString("name"),
                rs->getString("role"),
                rs->getString("specialty"),
                rs->getString("department")
            });
        }
        if (rows.empty()) {
            std::cout << "No investigators found.\n";
        } else {
            ui_.printTable({"ID", "Name", "Role", "Specialty", "Department"}, rows);
        }
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Investigators::readOne() {
    ui_.printHeader("Read Investigator");
    const int id = ui_.readInt("Investigator ID: ", 1, 1000000);
    try {
        auto stmt = db_.prepare(
            "SELECT investigator_id, name, role, specialty, department "
            "FROM investigators WHERE investigator_id = ?");
        stmt->setInt(1, id);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        if (!rs->next()) {
            ui_.printError("Investigator not found.");
            ui_.pause();
            return;
        }
        std::vector<std::vector<std::string>> rows = {{
            std::to_string(rs->getInt("investigator_id")),
            rs->getString("name"),
            rs->getString("role"),
            rs->getString("specialty"),
            rs->getString("department")
        }};
        ui_.printTable({"ID", "Name", "Role", "Specialty", "Department"}, rows);
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Investigators::update(const Session& session) {
    ui_.printHeader("Update Investigator");
    const int id = ui_.readInt("Investigator ID: ", 1, 1000000);
    try {
        auto load = db_.prepare(
            "SELECT name, role, specialty, department FROM investigators WHERE investigator_id = ?");
        load->setInt(1, id);
        std::unique_ptr<sql::ResultSet> rs(load->executeQuery());
        if (!rs->next()) {
            ui_.printError("Investigator not found.");
            ui_.pause();
            return;
        }
        std::string name = rs->getString("name");
        std::string role = rs->getString("role");
        std::string specialty = rs->getString("specialty");
        std::string department = rs->getString("department");
        std::cout << "Leave blank to keep the current value.\n";
        std::cout << "Current name: " << name << "\n";
        const std::string newName = ui_.readOptional("New name: ");
        if (!newName.empty()) name = newName;
        std::cout << "Current role: " << role << "\n";
        const std::string newRole = ui_.readOptional("New role: ");
        if (!newRole.empty()) role = newRole;
        std::cout << "Current specialty: " << specialty << "\n";
        const std::string newSpecialty = ui_.readOptional("New specialty: ");
        if (!newSpecialty.empty()) specialty = newSpecialty;
        std::cout << "Current department: " << department << "\n";
        const std::string newDept = ui_.readOptional("New department: ");
        if (!newDept.empty()) department = newDept;

        auto stmt = db_.prepare(
            "UPDATE investigators SET name = ?, role = ?, specialty = ?, department = ? "
            "WHERE investigator_id = ?");
        stmt->setString(1, name);
        stmt->setString(2, role);
        stmt->setString(3, specialty);
        stmt->setString(4, department);
        stmt->setInt(5, id);
        stmt->executeUpdate();
        audit_.write(session.userId, "UPDATE", "investigator_id=" + std::to_string(id), true);
        ui_.printSuccess("Investigator updated.");
    } catch (const std::exception& ex) {
        fail(ui_, ex);
        audit_.write(session.userId, "UPDATE", "investigator", false);
    }
    ui_.pause();
}

void Investigators::remove(const Session& session) {
    ui_.printHeader("Delete Investigator");
    const int id = ui_.readInt("Investigator ID: ", 1, 1000000);
    if (!ui_.confirm("Permanently delete this investigator?")) {
        ui_.pause();
        return;
    }
    try {
        auto check = db_.prepare(
            "SELECT COUNT(*) AS n FROM cases WHERE assigned_investigator_id = ?");
        check->setInt(1, id);
        std::unique_ptr<sql::ResultSet> rs(check->executeQuery());
        rs->next();
        if (rs->getInt("n") > 0) {
            ui_.printError("Cannot delete: investigator is assigned to one or more cases.");
            audit_.write(session.userId, "DELETE", "investigator_id=" + std::to_string(id), false);
            ui_.pause();
            return;
        }
        auto stmt = db_.prepare("DELETE FROM investigators WHERE investigator_id = ?");
        stmt->setInt(1, id);
        const int changed = stmt->executeUpdate();
        if (changed == 0) {
            ui_.printError("Investigator not found.");
            audit_.write(session.userId, "DELETE", "investigator_id=" + std::to_string(id), false);
        } else {
            audit_.write(session.userId, "DELETE", "investigator_id=" + std::to_string(id), true);
            ui_.printSuccess("Investigator deleted.");
        }
    } catch (const std::exception& ex) {
        fail(ui_, ex);
        audit_.write(session.userId, "DELETE", "investigator", false);
    }
    ui_.pause();
}

void Investigators::search() {
    ui_.printHeader("Search Investigators");
    std::cout << "Leave a field blank to skip it.\n";
    const int id = ui_.readOptionalInt("Investigator ID: ", 1, 1000000);
    const std::string name = ui_.readOptional("Name contains: ");
    const std::string specialty = ui_.readOptional("Specialty contains: ");
    try {
        auto stmt = db_.prepare(
            "SELECT investigator_id, name, role, specialty, department FROM investigators "
            "WHERE (? = 0 OR investigator_id = ?) "
            "AND (? = '' OR name LIKE CONCAT('%', ?, '%')) "
            "AND (? = '' OR specialty LIKE CONCAT('%', ?, '%')) "
            "ORDER BY investigator_id");
        const int idValue = id < 0 ? 0 : id;
        stmt->setInt(1, idValue);
        stmt->setInt(2, idValue);
        stmt->setString(3, name);
        stmt->setString(4, name);
        stmt->setString(5, specialty);
        stmt->setString(6, specialty);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::vector<std::string>> rows;
        while (rs->next()) {
            rows.push_back({
                std::to_string(rs->getInt("investigator_id")),
                rs->getString("name"),
                rs->getString("role"),
                rs->getString("specialty"),
                rs->getString("department")
            });
        }
        if (rows.empty()) {
            std::cout << "No matches.\n";
        } else {
            ui_.printTable({"ID", "Name", "Role", "Specialty", "Department"}, rows);
        }
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}
