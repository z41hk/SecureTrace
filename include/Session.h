#pragma once

#include <chrono>
#include <string>

enum class Role {
    Admin,
    Investigator,
    Viewer
};

inline Role roleFromString(const std::string& value) {
    if (value == "admin") return Role::Admin;
    if (value == "investigator") return Role::Investigator;
    return Role::Viewer;
}

inline const char* roleToString(Role role) {
    switch (role) {
        case Role::Admin: return "admin";
        case Role::Investigator: return "investigator";
        default: return "viewer";
    }
}

struct Session {
    int userId = 0;
    std::string username;
    Role role = Role::Viewer;
    bool authenticated = false;
    std::chrono::steady_clock::time_point lastActivity{};

    void clear() {
        userId = 0;
        username.clear();
        role = Role::Viewer;
        authenticated = false;
        lastActivity = {};
    }

    void touch() {
        lastActivity = std::chrono::steady_clock::now();
    }

    bool timedOut(int minutes) const {
        if (!authenticated) return false;
        const auto elapsed = std::chrono::steady_clock::now() - lastActivity;
        return elapsed > std::chrono::minutes(minutes);
    }
};
