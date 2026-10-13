#pragma once

#include "AuditLog.h"
#include "Config.h"
#include "ConsoleUI.h"
#include "DatabaseConnection.h"
#include "Session.h"

#include <string>

class Users {
public:
    Users(DatabaseConnection& db, AuditLog& audit, const Config& config, ConsoleUI& ui);

    bool login(Session& session);
    bool authenticate(const std::string& username, const std::string& password,
                      Session& session, bool interactive);
    void logout(Session& session);
    bool hasPermission(const Session& session, const std::string& feature) const;
    bool checkRole(const Session& session, Role required) const;
    void showMenu(Session& session);
    bool passwordMeetsPolicy(const std::string& password) const;
    std::string createSalt() const;
    std::string hashWithSalt(const std::string& password, const std::string& saltHex) const;
    std::string lastLoginTime(int userId) const;

private:
    void createUser(const Session& session);
    void listUsers();
    void readUser();
    void updateUser(const Session& session);
    void disableUser(const Session& session);
    void searchUsers();
    void unlockUser(const Session& session);

    bool verifyPassword(const std::string& password, const std::string& saltHex,
                        const std::string& hashHex) const;
    std::string hashPassword(const std::string& password, const std::string& saltHex) const;
    std::string generateSalt() const;
    bool isStrongPassword(const std::string& password) const;
    void recordFailedLogin(int userId);
    void resetLockout(int userId);

    static std::string toHex(const unsigned char* data, std::size_t length);
    static bool fromHex(const std::string& hex, unsigned char* out, std::size_t outLen);

    DatabaseConnection& db_;
    AuditLog& audit_;
    const Config& config_;
    ConsoleUI& ui_;
};
