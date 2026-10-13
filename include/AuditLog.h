#pragma once

#include "DatabaseConnection.h"

#include <string>

class AuditLog {
public:
    explicit AuditLog(DatabaseConnection& db);
    void write(int userId, const std::string& action, const std::string& target, bool success);

private:
    DatabaseConnection& db_;
};
