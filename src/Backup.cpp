#include "Backup.h"
#include "Logger.h"

#include <cppconn/exception.h>
#include <cppconn/resultset.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::string stamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return out.str();
}

std::string sqlLiteral(sql::ResultSet& rs, const std::string& col) {
    if (rs.isNull(col)) {
        return "NULL";
    }
    std::string value = rs.getString(col);
    std::string escaped = "'";
    for (char ch : value) {
        if (ch == '\'') {
            escaped += "''";
        } else {
            escaped += ch;
        }
    }
    escaped += "'";
    return escaped;
}

std::string csvCell(sql::ResultSet& rs, const std::string& col) {
    if (rs.isNull(col)) {
        return "";
    }
    std::string value = rs.getString(col);
    bool quote = false;
    for (char ch : value) {
        if (ch == ',' || ch == '"' || ch == '\n' || ch == '\r') {
            quote = true;
            break;
        }
    }
    if (!quote) {
        return value;
    }
    std::string out = "\"";
    for (char ch : value) {
        out += (ch == '"') ? "\"\"" : std::string(1, ch);
    }
    out += '"';
    return out;
}

void fail(ConsoleUI& ui, const std::exception& ex) {
    Logger::instance().error(std::string("backup: ") + ex.what());
    ui.printError("Operation failed.");
}
}

Backup::Backup(DatabaseConnection& db, AuditLog& audit, ConsoleUI& ui)
    : db_(db), audit_(audit), ui_(ui) {}

void Backup::showMenu(const Session& session) {
    if (!session.authenticated || session.role != Role::Admin) {
        ui_.printError("Access denied.");
        ui_.pause();
        return;
    }
    bool back = false;
    while (!back) {
        const int choice = ui_.showMenu("Backup / Export", {
            "Export all tables (CSV + SQL)",
            "Back"
        });
        if (choice == 1) {
            exportAll(session);
        } else {
            back = true;
        }
    }
}

std::string Backup::exportDatabase(const Session& session) {
    std::filesystem::create_directories("reports/backups");
    const std::string folder = "reports/backups/backup_" + stamp();
    std::filesystem::create_directories(folder);

    const std::vector<std::pair<std::string, std::vector<std::string>>> tables = {
        {"investigators", {"investigator_id", "name", "role", "specialty", "department"}},
        {"users", {"user_id", "username", "password_hash", "salt", "role",
                   "failed_attempts", "locked_until", "is_active"}},
        {"cases", {"case_id", "title", "type", "priority", "status", "opened_date",
                   "deadline", "closed_date", "assigned_investigator_id", "is_active"}},
        {"evidence", {"evidence_id", "case_id", "description", "type", "sha256_hash",
                      "size_mb", "collected_date", "storage_location", "verified", "is_active"}},
        {"custody_transfers", {"transfer_id", "evidence_id", "from_person", "to_person",
                               "transfer_datetime", "reason"}},
        {"audit_log", {"audit_id", "user_id", "action", "target", "timestamp", "result"}}
    };

    for (const auto& table : tables) {
        writeTableCsv(folder, table.first, table.second);
        writeInsertSql(folder, table.first, table.second);
    }
    audit_.write(session.userId, "BACKUP_EXPORT", folder, true);
    return folder;
}

void Backup::exportAll(const Session& session) {
    ui_.printHeader("Database backup");
    try {
        const std::string folder = exportDatabase(session);
        ui_.printSuccess("Backup written to " + folder);
    } catch (const std::exception& ex) {
        fail(ui_, ex);
        audit_.write(session.userId, "BACKUP_EXPORT", "all tables", false);
    }
    ui_.pause();
}

void Backup::writeTableCsv(const std::string& folder, const std::string& table,
                           const std::vector<std::string>& columns) {
    std::ostringstream sql;
    sql << "SELECT ";
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) sql << ", ";
        sql << columns[i];
    }
    sql << " FROM " << table;
    auto stmt = db_.prepare(sql.str());
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    std::ofstream file(folder + "/" + table + ".csv");
    if (!file) {
        throw std::runtime_error("cannot write csv for " + table);
    }
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) file << ",";
        file << columns[i];
    }
    file << "\n";
    while (rs->next()) {
        for (std::size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) file << ",";
            file << csvCell(*rs, columns[i]);
        }
        file << "\n";
    }
}

void Backup::writeInsertSql(const std::string& folder, const std::string& table,
                            const std::vector<std::string>& columns) {
    std::ostringstream sql;
    sql << "SELECT ";
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) sql << ", ";
        sql << columns[i];
    }
    sql << " FROM " << table;
    auto stmt = db_.prepare(sql.str());
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    std::ofstream file(folder + "/" + table + ".sql");
    if (!file) {
        throw std::runtime_error("cannot write sql for " + table);
    }
    file << "-- Backup of " << table << "\n";
    while (rs->next()) {
        file << "INSERT INTO " << table << " (";
        for (std::size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) file << ", ";
            file << columns[i];
        }
        file << ") VALUES (";
        for (std::size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) file << ", ";
            file << sqlLiteral(*rs, columns[i]);
        }
        file << ");\n";
    }
}
