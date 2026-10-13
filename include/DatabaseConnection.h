#pragma once

#include "Config.h"

#include <cppconn/prepared_statement.h>
#include <memory>
#include <string>

namespace sql {
class Connection;
}

class DatabaseConnection {
public:
    explicit DatabaseConnection(const Config& config);
    ~DatabaseConnection();

    DatabaseConnection(const DatabaseConnection&) = delete;
    DatabaseConnection& operator=(const DatabaseConnection&) = delete;

    void connect();
    void disconnect();
    bool isOpen() const;

    std::unique_ptr<sql::PreparedStatement> prepare(const std::string& sql);
    void setAppUserId(int userId);
    void clearAppUserId();

private:
    const Config& config_;
    std::unique_ptr<sql::Connection> connection_;
};
