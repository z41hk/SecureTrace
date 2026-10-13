#pragma once

#include "AuditLog.h"
#include "ConsoleUI.h"
#include "DatabaseConnection.h"
#include "Session.h"

#include <string>
#include <vector>

class Backup {
public:
    Backup(DatabaseConnection& db, AuditLog& audit, ConsoleUI& ui);
    void showMenu(const Session& session);
    std::string exportDatabase(const Session& session);

private:
    void exportAll(const Session& session);
    void writeTableCsv(const std::string& folder, const std::string& table,
                       const std::vector<std::string>& columns);
    void writeInsertSql(const std::string& folder, const std::string& table,
                        const std::vector<std::string>& columns);

    DatabaseConnection& db_;
    AuditLog& audit_;
    ConsoleUI& ui_;
};
