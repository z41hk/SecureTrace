#include "AuditLog.h"
#include "Logger.h"

#include <cppconn/exception.h>
#include <cppconn/prepared_statement.h>

AuditLog::AuditLog(DatabaseConnection& db) : db_(db) {}

void AuditLog::write(int userId, const std::string& action, const std::string& target, bool success) {
    try {
        auto stmt = db_.prepare(
            "INSERT INTO audit_log (user_id, action, target, result) VALUES (?, ?, ?, ?)");
        if (userId > 0) {
            stmt->setInt(1, userId);
        } else {
            stmt->setNull(1, 0);
        }
        stmt->setString(2, action);
        stmt->setString(3, target);
        stmt->setString(4, success ? "SUCCESS" : "FAIL");
        stmt->execute();
    } catch (const sql::SQLException& ex) {
        Logger::instance().error(std::string("audit write failed: ") + ex.what());
    } catch (const std::exception& ex) {
        Logger::instance().error(std::string("audit write failed: ") + ex.what());
    }
}
