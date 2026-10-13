#pragma once

#include <string>

class Config {
public:
    bool load(const std::string& path);

    const std::string& dbHost() const { return dbHost_; }
    int dbPort() const { return dbPort_; }
    const std::string& dbName() const { return dbName_; }
    const std::string& dbUser() const { return dbUser_; }
    const std::string& dbPassword() const { return dbPassword_; }

    int pbkdf2Iterations() const { return pbkdf2Iterations_; }
    int sessionTimeoutMinutes() const { return sessionTimeoutMinutes_; }
    int lockoutMinutes() const { return lockoutMinutes_; }
    int maxFailedAttempts() const { return maxFailedAttempts_; }

    static std::string findPath();

private:
    std::string dbHost_ = "127.0.0.1";
    int dbPort_ = 3306;
    std::string dbName_ = "securetrace";
    std::string dbUser_ = "securetrace_app";
    std::string dbPassword_;
    int pbkdf2Iterations_ = 100000;
    int sessionTimeoutMinutes_ = 10;
    int lockoutMinutes_ = 5;
    int maxFailedAttempts_ = 3;
};
