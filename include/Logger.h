#pragma once

#include <mutex>
#include <string>

class Logger {
public:
    static Logger& instance();
    void init(const std::string& path);
    void info(const std::string& message);
    void error(const std::string& message);

private:
    Logger() = default;
    void write(const std::string& level, const std::string& message);

    std::string path_;
    std::mutex mutex_;
};
