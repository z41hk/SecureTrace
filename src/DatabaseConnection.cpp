#include "DatabaseConnection.h"
#include "Logger.h"

#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <mysql_driver.h>
#include <stdexcept>
#include <string>

DatabaseConnection::DatabaseConnection(const Config& config) : config_(config) {}

DatabaseConnection::~DatabaseConnection() {
    disconnect();
}

void DatabaseConnection::connect() {
    try {
        sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();
        const std::string url = "tcp://" + config_.dbHost() + ":" + std::to_string(config_.dbPort());
        connection_.reset(driver->connect(url, config_.dbUser(), config_.dbPassword()));
        connection_->setSchema(config_.dbName());
        Logger::instance().info("Connected to database " + config_.dbName());
    } catch (const sql::SQLException& ex) {
        Logger::instance().error(std::string("DB connect failed: ") + ex.what());
        throw std::runtime_error("Operation failed: could not connect to the database.");
    }
}

void DatabaseConnection::disconnect() {
    if (connection_) {
        try {
            connection_->close();
        } catch (...) {
        }
        connection_.reset();
    }
}

bool DatabaseConnection::isOpen() const {
    return connection_ && !connection_->isClosed();
}

std::unique_ptr<sql::PreparedStatement> DatabaseConnection::prepare(const std::string& sql) {
    if (!isOpen()) {
        throw std::runtime_error("Operation failed: database is not connected.");
    }
    try {
        return std::unique_ptr<sql::PreparedStatement>(connection_->prepareStatement(sql));
    } catch (const sql::SQLException& ex) {
        Logger::instance().error(std::string("prepare failed: ") + ex.what() + " SQL=" + sql);
        throw std::runtime_error("Operation failed.");
    }
}

void DatabaseConnection::setAppUserId(int userId) {
    auto stmt = prepare("SET @app_user_id = ?");
    stmt->setInt(1, userId);
    stmt->executeUpdate();
}

void DatabaseConnection::clearAppUserId() {
    auto stmt = prepare("SET @app_user_id = NULL");
    stmt->executeUpdate();
}
