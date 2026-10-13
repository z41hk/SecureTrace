#include "Analysis.h"
#include "AuditLog.h"
#include "Backup.h"
#include "Cases.h"
#include "Config.h"
#include "ConsoleUI.h"
#include "CustodyTransfers.h"
#include "DatabaseConnection.h"
#include "Evidence.h"
#include "Investigators.h"
#include "Logger.h"
#include "Reports.h"
#include "Session.h"
#include "Users.h"

#include <cppconn/resultset.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
void ensureRuntimeDirs() {
    std::filesystem::create_directories("logs");
    std::filesystem::create_directories("reports");
    std::filesystem::create_directories("config");
}

struct MenuChoice {
    std::string label;
    std::string feature;
};

void printHelp(ConsoleUI& ui) {
    ui.printHeader("Help");
    std::cout << "Navigation: enter the option number. 0 or b goes back.\n";
    std::cout << "Forms: type c to cancel a field. y/n confirms delete or close.\n";
    std::cout << "Tables: n next page, p previous page, Enter/0 done.\n";
    std::cout << "Flags: --demo resets the database from sql/schema.sql and sql/seed.sql.\n";
    std::cout << "       --help or ? shows this screen.\n";
    ui.pause();
}

void printAbout(ConsoleUI& ui) {
    ui.printHeader("About");
    std::cout << "SecureTrace 1.0 - Digital Evidence & Case Management\n";
    std::cout << "UTeM, Faculty of AI and Cyber Security (FTKKI)\n";
    std::cout << "Defensive record-keeping only. No scanning or exploitation tools.\n";
    ui.pause();
}

int scalarInt(DatabaseConnection& db, const std::string& sql) {
    auto stmt = db.prepare(sql);
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    if (rs->next()) {
        return rs->getInt(1);
    }
    return 0;
}

void showDashboard(DatabaseConnection& db, Users& users, ConsoleUI& ui, const Session& session) {
    ui.printHeader("Dashboard");
    try {
        const int openCases = scalarInt(db,
            "SELECT COUNT(*) FROM cases WHERE is_active = 1 AND status IN ('Open', 'In Progress')");
        const int overdue = scalarInt(db,
            "SELECT COUNT(*) FROM cases WHERE is_active = 1 AND status <> 'Closed' AND deadline < CURDATE()");
        const int unverified = scalarInt(db,
            "SELECT COUNT(*) FROM evidence WHERE is_active = 1 AND verified = 0");
        const std::string lastLogin = users.lastLoginTime(session.userId);
        ui.printTable({"Metric", "Value"}, {
            {"Open / in-progress cases", std::to_string(openCases)},
            {"Overdue cases", std::to_string(overdue)},
            {"Unverified evidence", std::to_string(unverified)},
            {"Last login", lastLogin}
        });
    } catch (const std::exception& ex) {
        Logger::instance().error(std::string("dashboard: ") + ex.what());
        ui.printError("Dashboard unavailable.");
    }
    ui.pause();
}

std::string findMysqlClient() {
#ifdef _WIN32
    const char* candidates[] = {
        "D:\\xampp\\mysql\\bin\\mysql.exe",
        "C:\\xampp\\mysql\\bin\\mysql.exe",
        "C:\\Program Files\\MySQL\\MySQL Server 8.0\\bin\\mysql.exe",
        "mysql.exe"
    };
#else
    const char* candidates[] = {"mysql"};
#endif
    for (const char* path : candidates) {
        if (std::string(path) == "mysql" || std::string(path) == "mysql.exe") {
            return path;
        }
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    return "mysql";
}

bool runDemoReset(const Config& config, ConsoleUI& ui) {
    ui.printWarning("This will drop and recreate the securetrace database from schema.sql and seed.sql.");
    if (!ui.confirm("Continue?")) {
        return false;
    }
    const std::string mysql = findMysqlClient();
    const std::string host = config.dbHost();
    const std::string port = std::to_string(config.dbPort());
    auto runSql = [&](const std::string& file) {
        std::ostringstream cmd;
        cmd << "\"" << mysql << "\" -h " << host << " -P " << port
            << " -u root < \"" << file << "\"";
        return std::system(cmd.str().c_str());
    };
    if (runSql("sql/schema.sql") != 0) {
        ui.printError("Could not apply sql/schema.sql. Run it as MySQL root.");
        return false;
    }
    std::system(("\"" + mysql + "\" -h " + host + " -P " + port +
                 " -u root < sql/create_app_user.sql").c_str());
    if (runSql("sql/seed.sql") != 0) {
        ui.printError("Could not apply sql/seed.sql.");
        return false;
    }
    ui.printSuccess("Demo database reset complete.");
    return true;
}
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    ConsoleUI ui;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h" || arg == "?" || arg == "/?") {
            ui.printBanner();
            printHelp(ui);
            return 0;
        }
    }

    ui.clearScreen();
    ui.printBanner();

    try {
        ensureRuntimeDirs();
        Logger::instance().init("logs/app.log");

        Config config;
        const std::string configPath = Config::findPath();
        if (configPath.empty() || !config.load(configPath)) {
            ui.printError("Startup check failed: config.ini is missing.");
            ui.printWarning("Copy config/config.example.ini to config/config.ini and set the database password.");
            ui.pause();
            return 1;
        }
        Logger::instance().info("Loaded config from " + configPath);

        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--demo") {
                runDemoReset(config, ui);
            }
        }

        DatabaseConnection db(config);
        try {
            db.connect();
        } catch (const std::exception&) {
            ui.printError("Startup check failed: database is unreachable.");
            ui.printWarning("Start MySQL/MariaDB, check host/port/user in config/config.ini, then retry.");
            ui.pause();
            return 1;
        }

        AuditLog audit(db);
        Users users(db, audit, config, ui);
        Cases cases(db, audit, ui);
        Evidence evidence(db, audit, ui);
        Investigators investigators(db, audit, ui);
        CustodyTransfers custody(db, audit, ui);
        Reports reports(db, audit, ui);
        Analysis analysis(db, ui);
        Backup backup(db, audit, ui);

        Session session;
        bool running = true;
        bool showDash = false;

        while (running) {
            if (session.authenticated && session.timedOut(config.sessionTimeoutMinutes())) {
                Logger::instance().info("Session timeout for " + session.username);
                audit.write(session.userId, "LOGOUT", "timeout username=" + session.username, true);
                try {
                    db.clearAppUserId();
                } catch (...) {
                }
                session.clear();
                ui.printError("Session timed out. Please log in again.");
                ui.pause();
            }

            ui.clearScreen();
            ui.printBanner();
            if (session.authenticated) {
                ui.printStatusBar(session.username, roleToString(session.role));
            }

            if (showDash && session.authenticated) {
                showDash = false;
                showDashboard(db, users, ui, session);
                continue;
            }

            if (!session.authenticated) {
                const int choice = ui.showMenu("Welcome", {"Login", "Help", "About", "Exit"});
                if (choice == 1) {
                    if (users.login(session)) {
                        showDash = true;
                    }
                    ui.pause();
                } else if (choice == 2) {
                    printHelp(ui);
                } else if (choice == 3) {
                    printAbout(ui);
                } else {
                    running = false;
                }
                continue;
            }

            std::vector<MenuChoice> items;
            if (users.hasPermission(session, "cases")) {
                items.push_back({"Cases", "cases"});
            }
            if (users.hasPermission(session, "evidence")) {
                items.push_back({"Evidence", "evidence"});
            }
            if (users.hasPermission(session, "investigators") || session.role == Role::Admin) {
                items.push_back({"Investigators", "investigators"});
            }
            if (users.hasPermission(session, "custody")) {
                items.push_back({"Custody Transfers", "custody"});
            }
            if (session.role == Role::Admin) {
                items.push_back({"User Management", "users"});
                items.push_back({"Audit Log", "audit"});
                items.push_back({"Backup / Export", "backup"});
            }
            if (users.hasPermission(session, "analysis") ||
                users.hasPermission(session, "reports_summary")) {
                items.push_back({"Analysis & Calculations", "analysis"});
            }
            if (users.hasPermission(session, "reports")) {
                items.push_back({"Reports", "reports"});
            }
            if (users.hasPermission(session, "reports_summary")) {
                items.push_back({"Summary Reports", "reports_summary"});
            }
            items.push_back({"Dashboard", "dashboard"});
            items.push_back({"Help", "help"});
            items.push_back({"About", "about"});
            items.push_back({"Logout", "logout"});
            items.push_back({"Exit", "exit"});

            std::vector<std::string> labels;
            labels.reserve(items.size());
            for (const auto& item : items) {
                labels.push_back(item.label);
            }

            const int selected = ui.showMenu("Main Menu", labels);
            if (selected == static_cast<int>(labels.size()) && labels.back() != "Exit") {
                continue;
            }
            const std::string& feature = items[static_cast<std::size_t>(selected - 1)].feature;
            session.touch();

            if (feature == "logout") {
                users.logout(session);
                ui.pause();
            } else if (feature == "exit") {
                users.logout(session);
                running = false;
            } else if (feature == "dashboard") {
                showDashboard(db, users, ui, session);
            } else if (feature == "help") {
                printHelp(ui);
            } else if (feature == "about") {
                printAbout(ui);
            } else if (feature == "cases" && users.hasPermission(session, "cases")) {
                cases.showMenu(session);
            } else if (feature == "evidence" && users.hasPermission(session, "evidence")) {
                evidence.showMenu(session);
            } else if (feature == "investigators" &&
                       (users.hasPermission(session, "investigators") || session.role == Role::Admin)) {
                investigators.showMenu(session);
            } else if (feature == "custody" && users.hasPermission(session, "custody")) {
                custody.showMenu(session);
            } else if (feature == "users" && users.hasPermission(session, "users")) {
                users.showMenu(session);
            } else if (feature == "analysis" &&
                       (users.hasPermission(session, "analysis") ||
                        users.hasPermission(session, "reports_summary"))) {
                analysis.showMenu(session);
            } else if (feature == "reports" && users.hasPermission(session, "reports")) {
                reports.showMenu(session);
            } else if (feature == "reports_summary" && users.hasPermission(session, "reports_summary")) {
                reports.showSummary(session);
            } else if (feature == "audit" && session.role == Role::Admin) {
                reports.auditReport(session);
            } else if (feature == "backup" && session.role == Role::Admin) {
                backup.showMenu(session);
            } else {
                ui.printError("Access denied.");
                ui.pause();
            }
        }

        db.disconnect();
        Logger::instance().info("Application exit");
        return 0;
    } catch (const std::exception& ex) {
        Logger::instance().error(std::string("fatal: ") + ex.what());
        ui.printError(ex.what());
        ui.pause();
        return 1;
    }
}
