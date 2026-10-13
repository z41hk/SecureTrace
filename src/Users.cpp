#include "Users.h"
#include "Logger.h"
#include "Validation.h"

#include <cppconn/exception.h>
#include <cppconn/resultset.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <cctype>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

Users::Users(DatabaseConnection& db, AuditLog& audit, const Config& config, ConsoleUI& ui)
    : db_(db), audit_(audit), config_(config), ui_(ui) {}

bool Users::login(Session& session) {
    ui_.printHeader("Login");
    const std::string username = ui_.readString("Username: ", 3, 32);
    const std::string password = ui_.readPassword("Password: ");
    return authenticate(username, password, session, true);
}

bool Users::authenticate(const std::string& username, const std::string& password,
                         Session& session, bool interactive) {
    try {
        auto stmt = db_.prepare(
            "SELECT user_id, username, password_hash, salt, role, failed_attempts, "
            "locked_until, is_active, "
            "(locked_until IS NOT NULL AND locked_until > NOW()) AS is_locked "
            "FROM users WHERE username = ?");
        stmt->setString(1, username);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

        if (!rs->next()) {
            audit_.write(0, "LOGIN", "username=" + username, false);
            if (interactive) {
                ui_.printError("Operation failed: invalid username or password.");
            }
            return false;
        }

        const int userId = rs->getInt("user_id");
        const bool active = rs->getBoolean("is_active");
        const bool locked = rs->getBoolean("is_locked");
        const std::string hashHex = rs->getString("password_hash");
        const std::string saltHex = rs->getString("salt");
        const std::string role = rs->getString("role");

        if (!active) {
            audit_.write(userId, "LOGIN", "username=" + username, false);
            if (interactive) {
                ui_.printError("Operation failed: account is disabled.");
            }
            return false;
        }

        // REQUIREMENT: lock account for 5 minutes after 3 failed logins
        if (locked) {
            audit_.write(userId, "LOGIN", "username=" + username + " locked", false);
            if (interactive) {
                ui_.printError("Operation failed: account is locked. Try again later.");
            }
            return false;
        }

        // REQUIREMENT: PBKDF2-HMAC-SHA256 with per-user random salt
        if (!verifyPassword(password, saltHex, hashHex)) {
            recordFailedLogin(userId);
            audit_.write(userId, "LOGIN", "username=" + username, false);
            if (interactive) {
                ui_.printError("Operation failed: invalid username or password.");
            }
            return false;
        }

        resetLockout(userId);
        db_.setAppUserId(userId);
        session.userId = userId;
        session.username = rs->getString("username");
        session.role = roleFromString(role);
        session.authenticated = true;
        session.touch();
        audit_.write(userId, "LOGIN", "username=" + username, true);
        Logger::instance().info("User logged in: " + username);
        if (interactive) {
            ui_.printSuccess("Welcome, " + session.username + " (" + role + ").");
        }
        return true;
    } catch (const sql::SQLException& ex) {
        Logger::instance().error(std::string("login failed: ") + ex.what());
        if (interactive) {
            ui_.printError("Operation failed.");
        }
        return false;
    } catch (const std::exception& ex) {
        Logger::instance().error(std::string("login failed: ") + ex.what());
        if (interactive) {
            ui_.printError("Operation failed.");
        }
        return false;
    }
}

std::string Users::lastLoginTime(int userId) const {
    try {
        auto stmt = db_.prepare(
            "SELECT timestamp FROM audit_log WHERE user_id = ? AND action = 'LOGIN' AND result = 'SUCCESS' "
            "ORDER BY timestamp DESC LIMIT 1 OFFSET 1");
        stmt->setInt(1, userId);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        if (rs->next()) {
            return rs->getString("timestamp");
        }
    } catch (...) {
    }
    return "none recorded";
}

void Users::logout(Session& session) {
    if (session.authenticated) {
        audit_.write(session.userId, "LOGOUT", "username=" + session.username, true);
        Logger::instance().info("User logged out: " + session.username);
    }
    try {
        db_.clearAppUserId();
    } catch (...) {
    }
    session.clear();
    ui_.printSuccess("Logged out.");
}

bool Users::hasPermission(const Session& session, const std::string& feature) const {
    // REQUIREMENT: RBAC re-checked in the backend before every action
    if (!session.authenticated) {
        return false;
    }
    if (session.role == Role::Admin) {
        return true;
    }
    if (session.role == Role::Investigator) {
        return feature == "cases" || feature == "evidence" ||
               feature == "custody" || feature == "reports" || feature == "analysis";
    }
    return feature == "reports_summary";
}

bool Users::checkRole(const Session& session, Role required) const {
    if (!session.authenticated) {
        return false;
    }
    if (session.role == Role::Admin) {
        return true;
    }
    return session.role == required;
}

void Users::showMenu(Session& session) {
    if (!hasPermission(session, "users")) {
        ui_.printError("Access denied.");
        ui_.pause();
        return;
    }
    bool back = false;
    while (!back) {
        const int choice = ui_.showMenu("User Management", {
            "Create", "List all", "Read by ID", "Update", "Disable (soft)",
            "Search", "Unlock account", "Back"
        });
        switch (choice) {
            case 1: createUser(session); break;
            case 2: listUsers(); break;
            case 3: readUser(); break;
            case 4: updateUser(session); break;
            case 5: disableUser(session); break;
            case 6: searchUsers(); break;
            case 7: unlockUser(session); break;
            default: back = true; break;
        }
    }
}

void Users::createUser(const Session& session) {
    ui_.printHeader("Create User");
    try {
        const std::string username = ui_.readString("Username: ", 3, 32);
        if (!Validation::isUsername(username)) {
            ui_.printError("Username must be 3-32 letters, digits or underscore.");
            ui_.pause();
            return;
        }
        const std::string password = ui_.readPassword("Password: ");
        const std::string confirm = ui_.readPassword("Confirm password: ");
        if (password != confirm) {
            ui_.printError("Passwords do not match.");
            ui_.pause();
            return;
        }
        if (!isStrongPassword(password)) {
            ui_.printError("Password needs 8+ characters, one digit and one symbol.");
            ui_.pause();
            return;
        }
        const std::vector<std::string> roles = {"admin", "investigator", "viewer"};
        const std::string role = roles[static_cast<std::size_t>(ui_.pick("Role: ", roles) - 1)];
        const std::string salt = generateSalt();
        const std::string hash = hashPassword(password, salt);

        auto stmt = db_.prepare(
            "INSERT INTO users (username, password_hash, salt, role, failed_attempts, locked_until, is_active) "
            "VALUES (?, ?, ?, ?, 0, NULL, 1)");
        stmt->setString(1, username);
        stmt->setString(2, hash);
        stmt->setString(3, salt);
        stmt->setString(4, role);
        stmt->executeUpdate();
        audit_.write(session.userId, "CREATE", "user username=" + username, true);
        ui_.printSuccess("User created.");
    } catch (const sql::SQLException& ex) {
        Logger::instance().error(std::string("users create: ") + ex.what());
        ui_.printError("Operation failed. Username may already exist.");
        audit_.write(session.userId, "CREATE", "user", false);
    } catch (const std::exception& ex) {
        Logger::instance().error(std::string("users create: ") + ex.what());
        ui_.printError("Operation failed.");
        audit_.write(session.userId, "CREATE", "user", false);
    }
    ui_.pause();
}

void Users::listUsers() {
    ui_.printHeader("All Users");
    try {
        auto stmt = db_.prepare(
            "SELECT user_id, username, role, failed_attempts, locked_until, is_active "
            "FROM users ORDER BY user_id");
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::vector<std::string>> rows;
        while (rs->next()) {
            rows.push_back({
                std::to_string(rs->getInt("user_id")),
                rs->getString("username"),
                rs->getString("role"),
                std::to_string(rs->getInt("failed_attempts")),
                rs->isNull("locked_until") ? "-" : rs->getString("locked_until"),
                rs->getBoolean("is_active") ? "yes" : "no"
            });
        }
        ui_.printTable({"ID", "Username", "Role", "Fails", "Locked until", "Active"}, rows);
    } catch (const std::exception& ex) {
        Logger::instance().error(std::string("users list: ") + ex.what());
        ui_.printError("Operation failed.");
    }
    ui_.pause();
}

void Users::readUser() {
    ui_.printHeader("Read User");
    const int id = ui_.readInt("User ID: ", 1, 1000000);
    try {
        auto stmt = db_.prepare(
            "SELECT user_id, username, role, failed_attempts, locked_until, is_active "
            "FROM users WHERE user_id = ?");
        stmt->setInt(1, id);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        if (!rs->next()) {
            ui_.printError("User not found.");
            ui_.pause();
            return;
        }
        std::vector<std::vector<std::string>> rows = {{
            std::to_string(rs->getInt("user_id")),
            rs->getString("username"),
            rs->getString("role"),
            std::to_string(rs->getInt("failed_attempts")),
            rs->isNull("locked_until") ? "-" : rs->getString("locked_until"),
            rs->getBoolean("is_active") ? "yes" : "no"
        }};
        ui_.printTable({"ID", "Username", "Role", "Fails", "Locked until", "Active"}, rows);
    } catch (const std::exception& ex) {
        Logger::instance().error(std::string("users read: ") + ex.what());
        ui_.printError("Operation failed.");
    }
    ui_.pause();
}

void Users::updateUser(const Session& session) {
    ui_.printHeader("Update User");
    const int id = ui_.readInt("User ID: ", 1, 1000000);
    try {
        auto load = db_.prepare("SELECT username, role, is_active FROM users WHERE user_id = ?");
        load->setInt(1, id);
        std::unique_ptr<sql::ResultSet> rs(load->executeQuery());
        if (!rs->next()) {
            ui_.printError("User not found.");
            ui_.pause();
            return;
        }
        const std::string username = rs->getString("username");
        std::string role = rs->getString("role");
        bool active = rs->getBoolean("is_active");

        if (id == session.userId && ui_.confirm("Change your own role?")) {
            ui_.printError("You cannot change your own role.");
            ui_.pause();
            return;
        }
        std::cout << "Current role: " << role << "\n";
        if (id != session.userId && ui_.confirm("Change role?")) {
            const std::vector<std::string> roles = {"admin", "investigator", "viewer"};
            role = roles[static_cast<std::size_t>(ui_.pick("Role: ", roles) - 1)];
        }
        std::cout << "Currently active: " << (active ? "yes" : "no") << "\n";
        if (id == session.userId) {
            std::cout << "You cannot disable your own account.\n";
        } else if (ui_.confirm("Toggle active flag?")) {
            active = !active;
        }

        bool changePassword = ui_.confirm("Reset password?");
        std::string salt;
        std::string hash;
        if (changePassword) {
            const std::string password = ui_.readPassword("New password: ");
            const std::string confirm = ui_.readPassword("Confirm password: ");
            if (password != confirm || !isStrongPassword(password)) {
                ui_.printError("Password invalid or does not match.");
                ui_.pause();
                return;
            }
            salt = generateSalt();
            hash = hashPassword(password, salt);
        }

        if (changePassword) {
            auto stmt = db_.prepare(
                "UPDATE users SET role = ?, is_active = ?, password_hash = ?, salt = ?, "
                "failed_attempts = 0, locked_until = NULL WHERE user_id = ?");
            stmt->setString(1, role);
            stmt->setInt(2, active ? 1 : 0);
            stmt->setString(3, hash);
            stmt->setString(4, salt);
            stmt->setInt(5, id);
            stmt->executeUpdate();
        } else {
            auto stmt = db_.prepare("UPDATE users SET role = ?, is_active = ? WHERE user_id = ?");
            stmt->setString(1, role);
            stmt->setInt(2, active ? 1 : 0);
            stmt->setInt(3, id);
            stmt->executeUpdate();
        }
        audit_.write(session.userId, "UPDATE", "user_id=" + std::to_string(id) + " username=" + username, true);
        ui_.printSuccess("User updated.");
    } catch (const std::exception& ex) {
        Logger::instance().error(std::string("users update: ") + ex.what());
        ui_.printError("Operation failed.");
        audit_.write(session.userId, "UPDATE", "user", false);
    }
    ui_.pause();
}

void Users::disableUser(const Session& session) {
    ui_.printHeader("Disable User");
    const int id = ui_.readInt("User ID: ", 1, 1000000);
    if (id == session.userId) {
        ui_.printError("You cannot disable your own account.");
        ui_.pause();
        return;
    }
    if (!ui_.confirm("Disable this account?")) {
        ui_.pause();
        return;
    }
    try {
        auto stmt = db_.prepare("UPDATE users SET is_active = 0 WHERE user_id = ? AND is_active = 1");
        stmt->setInt(1, id);
        const int changed = stmt->executeUpdate();
        if (changed == 0) {
            ui_.printError("User not found or already disabled.");
            audit_.write(session.userId, "DELETE", "user_id=" + std::to_string(id), false);
        } else {
            audit_.write(session.userId, "DELETE", "user_id=" + std::to_string(id), true);
            ui_.printSuccess("User disabled.");
        }
    } catch (const std::exception& ex) {
        Logger::instance().error(std::string("users disable: ") + ex.what());
        ui_.printError("Operation failed.");
        audit_.write(session.userId, "DELETE", "user", false);
    }
    ui_.pause();
}

void Users::searchUsers() {
    ui_.printHeader("Search Users");
    std::cout << "Leave a field blank to skip it.\n";
    const int id = ui_.readOptionalInt("User ID: ", 1, 1000000);
    const std::string username = ui_.readOptional("Username contains: ");
    const std::string role = ui_.readOptional("Role (admin/investigator/viewer): ");
    try {
        auto stmt = db_.prepare(
            "SELECT user_id, username, role, is_active FROM users "
            "WHERE (? = 0 OR user_id = ?) "
            "AND (? = '' OR username LIKE CONCAT('%', ?, '%')) "
            "AND (? = '' OR role = ?) "
            "ORDER BY user_id");
        const int idValue = id < 0 ? 0 : id;
        stmt->setInt(1, idValue);
        stmt->setInt(2, idValue);
        stmt->setString(3, username);
        stmt->setString(4, username);
        stmt->setString(5, role);
        stmt->setString(6, role);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::vector<std::string>> rows;
        while (rs->next()) {
            rows.push_back({
                std::to_string(rs->getInt("user_id")),
                rs->getString("username"),
                rs->getString("role"),
                rs->getBoolean("is_active") ? "yes" : "no"
            });
        }
        if (rows.empty()) {
            std::cout << "No matches.\n";
        } else {
            ui_.printTable({"ID", "Username", "Role", "Active"}, rows);
        }
    } catch (const std::exception& ex) {
        Logger::instance().error(std::string("users search: ") + ex.what());
        ui_.printError("Operation failed.");
    }
    ui_.pause();
}

void Users::unlockUser(const Session& session) {
    ui_.printHeader("Unlock Account");
    const int id = ui_.readInt("User ID: ", 1, 1000000);
    try {
        auto stmt = db_.prepare(
            "UPDATE users SET failed_attempts = 0, locked_until = NULL WHERE user_id = ?");
        stmt->setInt(1, id);
        const int changed = stmt->executeUpdate();
        if (changed == 0) {
            ui_.printError("User not found.");
            audit_.write(session.userId, "UPDATE", "unlock user_id=" + std::to_string(id), false);
        } else {
            audit_.write(session.userId, "UPDATE", "unlock user_id=" + std::to_string(id), true);
            ui_.printSuccess("Account unlocked.");
        }
    } catch (const std::exception& ex) {
        Logger::instance().error(std::string("users unlock: ") + ex.what());
        ui_.printError("Operation failed.");
        audit_.write(session.userId, "UPDATE", "unlock", false);
    }
    ui_.pause();
}

bool Users::verifyPassword(const std::string& password, const std::string& saltHex,
                           const std::string& hashHex) const {
    const std::string computed = hashPassword(password, saltHex);
    if (computed.size() != hashHex.size()) {
        return false;
    }
    unsigned char diff = 0;
    for (std::size_t i = 0; i < computed.size(); ++i) {
        diff |= static_cast<unsigned char>(computed[i] ^ hashHex[i]);
    }
    return diff == 0;
}

std::string Users::hashPassword(const std::string& password, const std::string& saltHex) const {
    unsigned char salt[16];
    if (!fromHex(saltHex, salt, sizeof(salt))) {
        throw std::runtime_error("Operation failed.");
    }
    unsigned char out[32];
    const int iterations = config_.pbkdf2Iterations();
    if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
                          salt, static_cast<int>(sizeof(salt)),
                          iterations, EVP_sha256(),
                          static_cast<int>(sizeof(out)), out) != 1) {
        throw std::runtime_error("Operation failed.");
    }
    return toHex(out, sizeof(out));
}

std::string Users::generateSalt() const {
    unsigned char salt[16];
    if (RAND_bytes(salt, sizeof(salt)) != 1) {
        throw std::runtime_error("Operation failed.");
    }
    return toHex(salt, sizeof(salt));
}

bool Users::passwordMeetsPolicy(const std::string& password) const {
    return isStrongPassword(password);
}

std::string Users::createSalt() const {
    return generateSalt();
}

std::string Users::hashWithSalt(const std::string& password, const std::string& saltHex) const {
    return hashPassword(password, saltHex);
}

bool Users::isStrongPassword(const std::string& password) const {
    if (password.size() < 8) {
        return false;
    }
    bool digit = false;
    bool symbol = false;
    for (unsigned char ch : password) {
        if (std::isdigit(ch)) digit = true;
        else if (!std::isalnum(ch)) symbol = true;
    }
    return digit && symbol;
}

void Users::recordFailedLogin(int userId) {
    auto increment = db_.prepare(
        "UPDATE users SET failed_attempts = failed_attempts + 1 WHERE user_id = ?");
    increment->setInt(1, userId);
    increment->executeUpdate();

    auto lock = db_.prepare(
        "UPDATE users SET locked_until = DATE_ADD(NOW(), INTERVAL ? MINUTE) "
        "WHERE user_id = ? AND failed_attempts >= ?");
    lock->setInt(1, config_.lockoutMinutes());
    lock->setInt(2, userId);
    lock->setInt(3, config_.maxFailedAttempts());
    lock->executeUpdate();
}

void Users::resetLockout(int userId) {
    auto stmt = db_.prepare(
        "UPDATE users SET failed_attempts = 0, locked_until = NULL WHERE user_id = ?");
    stmt->setInt(1, userId);
    stmt->executeUpdate();
}

std::string Users::toHex(const unsigned char* data, std::size_t length) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.resize(length * 2);
    for (std::size_t i = 0; i < length; ++i) {
        out[i * 2] = digits[(data[i] >> 4) & 0x0F];
        out[i * 2 + 1] = digits[data[i] & 0x0F];
    }
    return out;
}

bool Users::fromHex(const std::string& hex, unsigned char* out, std::size_t outLen) {
    if (hex.size() != outLen * 2) {
        return false;
    }
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < outLen; ++i) {
        const int hi = nibble(hex[i * 2]);
        const int lo = nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = static_cast<unsigned char>((hi << 4) | lo);
    }
    return true;
}
