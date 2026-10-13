#include "Logger.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::init(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    path_ = path;
}

void Logger::info(const std::string& message) {
    write("INFO", message);
}

void Logger::error(const std::string& message) {
    write("ERROR", message);
}

void Logger::write(const std::string& level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (path_.empty()) {
        return;
    }
    std::ofstream file(path_, std::ios::app);
    if (!file) {
        return;
    }
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    file << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
         << " [" << level << "] " << message << '\n';
}
