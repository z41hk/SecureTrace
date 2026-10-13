#pragma once

#ifdef _WIN32

#include "AuditLog.h"
#include "Backup.h"
#include "Config.h"
#include "ConsoleUI.h"
#include "DatabaseConnection.h"
#include "Session.h"
#include "Users.h"

#include <windows.h>
#include <memory>
#include <string>
#include <vector>

class GuiApp {
public:
    GuiApp();
    int run();

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    void onCreate(HWND hwnd);
    void onCommand(int id);
    void onTimer();
    void showLogin(bool visible);
    void showMain(bool visible);
    void rebuildNav();
    void setModule(const std::string& name);
    void refresh();
    void doLogin();
    void doLogout();
    void doCreate();
    void doUpdate();
    void doDelete();
    void doSearch();
    void doVerify();
    void doAnalysis(int which);
    void doReport(int which);
    void doExport();
    void doBackup();
    void loadSelected();
    void fillList(const std::vector<std::string>& headers,
                  const std::vector<std::vector<std::string>>& rows);
    std::string field(int id) const;
    void setField(int id, const std::string& value);
    void clearFields();
    bool requireRole(const std::string& feature);
    bool maskSensitive() const;

    HWND hwnd_ = nullptr;
    HWND list_ = nullptr;
    HWND status_ = nullptr;
    HWND output_ = nullptr;
    std::vector<HWND> navButtons_;
    std::string module_ = "cases";
    std::string lastReport_;

    Config config_;
    ConsoleUI ui_;
    std::unique_ptr<DatabaseConnection> db_;
    std::unique_ptr<AuditLog> audit_;
    std::unique_ptr<Users> users_;
    std::unique_ptr<Backup> backup_;
    Session session_;
};

#endif
