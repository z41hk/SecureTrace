#include "Config.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace {
std::string trim(const std::string& value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}
}

bool Config::load(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }

    std::string section;
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));

        if (section == "database") {
            if (key == "host") dbHost_ = value;
            else if (key == "port") {
                try { dbPort_ = std::stoi(value); } catch (...) {}
            }
            else if (key == "name") dbName_ = value;
            else if (key == "user") dbUser_ = value;
            else if (key == "password") dbPassword_ = value;
        } else if (section == "security") {
            try {
                if (key == "pbkdf2_iterations") {
                    pbkdf2Iterations_ = std::max(100000, std::stoi(value));
                } else if (key == "session_timeout_minutes") {
                    sessionTimeoutMinutes_ = std::stoi(value);
                } else if (key == "lockout_minutes") {
                    lockoutMinutes_ = std::stoi(value);
                } else if (key == "max_failed_attempts") {
                    maxFailedAttempts_ = std::stoi(value);
                }
            } catch (...) {
            }
        }
    }
    return true;
}

std::string Config::findPath() {
    const char* candidates[] = {
        "config/config.ini",
        "../config/config.ini",
        "../../config/config.ini",
        "config/config.example.ini",
        "../config/config.example.ini"
    };
    for (const char* path : candidates) {
        std::ifstream test(path);
        if (test) {
            return path;
        }
    }
    return {};
}
