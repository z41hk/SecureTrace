#ifdef _WIN32

#include "GuiApp.h"
#include "HashUtil.h"
#include "Logger.h"
#include "Validation.h"
#include "WinUtil.h"

#include <cppconn/exception.h>
#include <cppconn/resultset.h>

#include <commctrl.h>
#include <commdlg.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
enum {
    IDC_USER = 101, IDC_PASS, IDC_LOGIN,
    IDC_LIST = 200, IDC_STATUS, IDC_OUTPUT,
    IDC_NAV0 = 300,
    IDC_CREATE = 401, IDC_UPDATE, IDC_DELETE, IDC_SEARCH, IDC_VERIFY,
    IDC_REFRESH, IDC_EXPORT, IDC_BACKUP, IDC_LOGOUT,
    IDC_F1 = 501, IDC_F2, IDC_F3, IDC_F4, IDC_F5, IDC_F6, IDC_F7, IDC_F8,
    IDC_C1 = 511, IDC_C2, IDC_C3,
    IDC_A1 = 601, IDC_A2, IDC_A3, IDC_A4, IDC_A5, IDC_A6,
    IDC_R1 = 701, IDC_R2, IDC_R3, IDC_R4, IDC_R5, IDC_R6
};

const UINT kTimerId = 1;

std::string cell(sql::ResultSet& rs, const std::string& col) {
    if (rs.isNull(col)) return "-";
    return rs.getString(col);
}

std::string yesNo(sql::ResultSet& rs, const std::string& col) {
    if (rs.isNull(col)) return "-";
    return rs.getBoolean(col) ? "yes" : "no";
}

std::string bar(int value, int maxValue, int width) {
    if (maxValue <= 0 || value <= 0) return std::string(static_cast<std::size_t>(width), ' ');
    int filled = (value * width) / maxValue;
    if (value > 0 && filled == 0) filled = 1;
    if (filled > width) filled = width;
    return std::string(static_cast<std::size_t>(filled), '#') +
           std::string(static_cast<std::size_t>(width - filled), ' ');
}

int toInt(const std::string& value, int fallback = 0) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}
}

GuiApp::GuiApp() = default;

int GuiApp::run() {
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    std::filesystem::create_directories("logs");
    std::filesystem::create_directories("reports");
    std::filesystem::create_directories("config");
    Logger::instance().init("logs/app.log");

    const std::string cfg = Config::findPath();
    if (cfg.empty() || !config_.load(cfg)) {
        errorBox(nullptr, "Could not load config.ini");
        return 1;
    }

    try {
        db_ = std::make_unique<DatabaseConnection>(config_);
        db_->connect();
        audit_ = std::make_unique<AuditLog>(*db_);
        users_ = std::make_unique<Users>(*db_, *audit_, config_, ui_);
        backup_ = std::make_unique<Backup>(*db_, *audit_, ui_);
    } catch (const std::exception& ex) {
        errorBox(nullptr, ex.what());
        return 1;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"SecureTraceGui";
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    RegisterClassW(&wc);

    hwnd_ = CreateWindowW(L"SecureTraceGui", L"SecureTrace",
                          WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                          80, 40, 1280, 780, nullptr, nullptr, wc.hInstance, this);
    if (!hwnd_) {
        return 1;
    }
    SetTimer(hwnd_, kTimerId, 30000, nullptr);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwnd_, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK GuiApp::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    GuiApp* app = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<GuiApp*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        app->hwnd_ = hwnd;
    } else {
        app = reinterpret_cast<GuiApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!app) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    switch (msg) {
        case WM_CREATE: app->onCreate(hwnd); return 0;
        case WM_COMMAND: app->onCommand(LOWORD(wParam)); return 0;
        case WM_NOTIFY: {
            auto hdr = reinterpret_cast<LPNMHDR>(lParam);
            if (hdr->idFrom == IDC_LIST && hdr->code == NM_CLICK) {
                app->loadSelected();
            }
            return 0;
        }
        case WM_TIMER: app->onTimer(); return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kTimerId);
            PostQuitMessage(0);
            return 0;
        default: return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void GuiApp::onCreate(HWND hwnd) {
    addStatic(hwnd, L"Username", 40, 40, 120, 20);
    addEdit(hwnd, IDC_USER, 40, 64, 260, 24, false);
    addStatic(hwnd, L"Password", 40, 100, 120, 20);
    addEdit(hwnd, IDC_PASS, 40, 124, 260, 24, true);
    addButton(hwnd, IDC_LOGIN, L"Login", 40, 164, 120, 32);

    addButton(hwnd, IDC_CREATE, L"Create", 170, 12, 90, 28);
    addButton(hwnd, IDC_UPDATE, L"Update", 266, 12, 90, 28);
    addButton(hwnd, IDC_DELETE, L"Delete", 362, 12, 90, 28);
    addButton(hwnd, IDC_SEARCH, L"Search", 458, 12, 90, 28);
    addButton(hwnd, IDC_VERIFY, L"Verify", 554, 12, 90, 28);
    addButton(hwnd, IDC_REFRESH, L"Refresh", 650, 12, 90, 28);
    addButton(hwnd, IDC_EXPORT, L"Export", 746, 12, 90, 28);
    addButton(hwnd, IDC_BACKUP, L"Backup", 842, 12, 90, 28);
    addButton(hwnd, IDC_LOGOUT, L"Logout", 938, 12, 90, 28);

    addButton(hwnd, IDC_A1, L"AGGREGATION", 170, 48, 120, 24);
    addButton(hwnd, IDC_A2, L"GROUPING", 296, 48, 100, 24);
    addButton(hwnd, IDC_A3, L"JOIN", 402, 48, 80, 24);
    addButton(hwnd, IDC_A4, L"SUBQUERY", 488, 48, 100, 24);
    addButton(hwnd, IDC_A5, L"UNARY", 594, 48, 80, 24);
    addButton(hwnd, IDC_A6, L"DECISION", 680, 48, 100, 24);
    addButton(hwnd, IDC_R1, L"R1 Status", 790, 48, 90, 24);
    addButton(hwnd, IDC_R2, L"R2 Lists", 886, 48, 80, 24);
    addButton(hwnd, IDC_R3, L"R3 Custody", 972, 48, 90, 24);
    addButton(hwnd, IDC_R4, L"R4 Chart", 1068, 48, 80, 24);
    addButton(hwnd, IDC_R5, L"R5 Trend", 1154, 48, 80, 24);
    addButton(hwnd, IDC_R6, L"R6 Audit", 170, 76, 90, 24);

    addStatic(hwnd, L"F1", 170, 108, 30, 18);
    addEdit(hwnd, IDC_F1, 204, 104, 160, 22, false);
    addStatic(hwnd, L"F2", 374, 108, 30, 18);
    addEdit(hwnd, IDC_F2, 408, 104, 160, 22, false);
    addStatic(hwnd, L"F3", 578, 108, 30, 18);
    addEdit(hwnd, IDC_F3, 612, 104, 160, 22, false);
    addStatic(hwnd, L"F4", 782, 108, 30, 18);
    addEdit(hwnd, IDC_F4, 816, 104, 160, 22, false);
    addStatic(hwnd, L"F5", 170, 136, 30, 18);
    addEdit(hwnd, IDC_F5, 204, 132, 160, 22, false);
    addStatic(hwnd, L"F6", 374, 136, 30, 18);
    addEdit(hwnd, IDC_F6, 408, 132, 160, 22, false);
    addStatic(hwnd, L"F7", 578, 136, 30, 18);
    addEdit(hwnd, IDC_F7, 612, 132, 160, 22, false);
    addStatic(hwnd, L"F8", 782, 136, 30, 18);
    addEdit(hwnd, IDC_F8, 816, 132, 160, 22, false);
    addCombo(hwnd, IDC_C1, 990, 104, 160, 200);
    addCombo(hwnd, IDC_C2, 990, 132, 160, 200);
    addCombo(hwnd, IDC_C3, 990, 160, 160, 200);

    list_ = addList(hwnd, IDC_LIST, 170, 196, 1080, 320);
    output_ = CreateWindowW(L"EDIT", L"",
                            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_READONLY,
                            170, 524, 1080, 160, hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_OUTPUT)),
                            GetModuleHandleW(nullptr), nullptr);
    status_ = CreateWindowW(L"STATIC", L"Logged out", WS_CHILD | WS_VISIBLE,
                            12, 700, 1240, 22, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUS)),
                            GetModuleHandleW(nullptr), nullptr);
    showMain(false);
    showLogin(true);
}

void GuiApp::showLogin(bool visible) {
    const int cmd = visible ? SW_SHOW : SW_HIDE;
    ShowWindow(GetDlgItem(hwnd_, IDC_USER), cmd);
    ShowWindow(GetDlgItem(hwnd_, IDC_PASS), cmd);
    ShowWindow(GetDlgItem(hwnd_, IDC_LOGIN), cmd);
}

void GuiApp::showMain(bool visible) {
    const int cmd = visible ? SW_SHOW : SW_HIDE;
    for (int id = IDC_CREATE; id <= IDC_LOGOUT; ++id) ShowWindow(GetDlgItem(hwnd_, id), cmd);
    for (int id = IDC_A1; id <= IDC_A6; ++id) ShowWindow(GetDlgItem(hwnd_, id), cmd);
    for (int id = IDC_R1; id <= IDC_R6; ++id) ShowWindow(GetDlgItem(hwnd_, id), cmd);
    for (int id = IDC_F1; id <= IDC_C3; ++id) ShowWindow(GetDlgItem(hwnd_, id), cmd);
    ShowWindow(list_, cmd);
    ShowWindow(output_, cmd);
    for (HWND btn : navButtons_) ShowWindow(btn, cmd);
}

void GuiApp::rebuildNav() {
    for (HWND btn : navButtons_) DestroyWindow(btn);
    navButtons_.clear();
    std::vector<std::pair<std::string, std::wstring>> items;
    if (users_->hasPermission(session_, "cases")) items.push_back({"cases", L"Cases"});
    if (users_->hasPermission(session_, "evidence")) items.push_back({"evidence", L"Evidence"});
    if (session_.role == Role::Admin) items.push_back({"investigators", L"Investigators"});
    if (users_->hasPermission(session_, "custody")) items.push_back({"custody", L"Custody"});
    if (session_.role == Role::Admin) {
        items.push_back({"users", L"Users"});
        items.push_back({"audit", L"Audit"});
        items.push_back({"backup", L"Backup"});
    }
    if (users_->hasPermission(session_, "analysis") || users_->hasPermission(session_, "reports_summary")) {
        items.push_back({"analysis", L"Analysis"});
    }
    if (users_->hasPermission(session_, "reports") || users_->hasPermission(session_, "reports_summary")) {
        items.push_back({"reports", L"Reports"});
    }
    int y = 196;
    int id = IDC_NAV0;
    for (const auto& item : items) {
        HWND btn = addButton(hwnd_, id, item.second.c_str(), 12, y, 148, 28);
        SetWindowLongPtrW(btn, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new std::string(item.first)));
        navButtons_.push_back(btn);
        y += 34;
        ++id;
    }
}

void GuiApp::onCommand(int id) {
    session_.touch();
    if (id == IDC_LOGIN) { doLogin(); return; }
    if (id == IDC_LOGOUT) { doLogout(); return; }
    if (id == IDC_CREATE) { doCreate(); return; }
    if (id == IDC_UPDATE) { doUpdate(); return; }
    if (id == IDC_DELETE) { doDelete(); return; }
    if (id == IDC_SEARCH) { doSearch(); return; }
    if (id == IDC_VERIFY) { doVerify(); return; }
    if (id == IDC_REFRESH) { refresh(); return; }
    if (id == IDC_EXPORT) { doExport(); return; }
    if (id == IDC_BACKUP) { doBackup(); return; }
    if (id >= IDC_A1 && id <= IDC_A6) { doAnalysis(id - IDC_A1 + 1); return; }
    if (id >= IDC_R1 && id <= IDC_R6) { doReport(id - IDC_R1 + 1); return; }
    if (id >= IDC_NAV0 && id < IDC_NAV0 + 20) {
        HWND btn = GetDlgItem(hwnd_, id);
        auto name = reinterpret_cast<std::string*>(GetWindowLongPtrW(btn, GWLP_USERDATA));
        if (name) setModule(*name);
    }
}

void GuiApp::onTimer() {
    if (session_.authenticated && session_.timedOut(config_.sessionTimeoutMinutes())) {
        audit_->write(session_.userId, "LOGOUT", "timeout username=" + session_.username, true);
        try { db_->clearAppUserId(); } catch (...) {}
        session_.clear();
        showMain(false);
        showLogin(true);
        setText(status_, "Session timed out.");
        errorBox(hwnd_, "Session timed out. Please log in again.");
    }
}

void GuiApp::doLogin() {
    Session next;
    const std::string user = field(IDC_USER);
    const std::string pass = field(IDC_PASS);
    if (!users_->authenticate(user, pass, next, false)) {
        errorBox(hwnd_, "Operation failed: invalid username or password.");
        return;
    }
    session_ = next;
    rebuildNav();
    showLogin(false);
    showMain(true);
    if (session_.role == Role::Viewer) {
        setModule("reports");
    } else {
        setModule("cases");
    }
    setText(status_, "Logged in as " + session_.username + " [" + roleToString(session_.role) + "]");
}

void GuiApp::doLogout() {
    if (session_.authenticated) {
        audit_->write(session_.userId, "LOGOUT", "username=" + session_.username, true);
        try { db_->clearAppUserId(); } catch (...) {}
    }
    session_.clear();
    showMain(false);
    showLogin(true);
    setText(status_, "Logged out");
}

bool GuiApp::requireRole(const std::string& feature) {
    if (!session_.authenticated) return false;
    if (feature == "analysis") {
        return users_->hasPermission(session_, "analysis") ||
               users_->hasPermission(session_, "reports_summary");
    }
    if (feature == "reports") {
        return users_->hasPermission(session_, "reports") ||
               users_->hasPermission(session_, "reports_summary");
    }
    if (feature == "investigators" || feature == "users" || feature == "audit" || feature == "backup") {
        return session_.role == Role::Admin;
    }
    return users_->hasPermission(session_, feature);
}

bool GuiApp::maskSensitive() const {
    return session_.role == Role::Viewer;
}

std::string GuiApp::field(int id) const {
    return getText(GetDlgItem(hwnd_, id));
}

void GuiApp::setField(int id, const std::string& value) {
    setText(GetDlgItem(hwnd_, id), value);
}

void GuiApp::clearFields() {
    for (int id = IDC_F1; id <= IDC_F8; ++id) setField(id, "");
}

void GuiApp::fillList(const std::vector<std::string>& headers,
                      const std::vector<std::vector<std::string>>& rows) {
    listHeaders(list_, headers);
    for (const auto& row : rows) listAddRow(list_, row);
}

void GuiApp::setModule(const std::string& name) {
    if ((name == "investigators" || name == "users" || name == "audit" || name == "backup") &&
        session_.role != Role::Admin) {
        errorBox(hwnd_, "Access denied.");
        return;
    }
    if ((name == "cases" || name == "evidence" || name == "custody") &&
        !users_->hasPermission(session_, name)) {
        errorBox(hwnd_, "Access denied.");
        return;
    }
    module_ = name;
    clearFields();
    comboAdd(GetDlgItem(hwnd_, IDC_C1), {"Fraud", "Data Breach", "Insider Threat", "Malware", "Other"});
    comboAdd(GetDlgItem(hwnd_, IDC_C2), {"Low", "Medium", "High", "Critical"});
    comboAdd(GetDlgItem(hwnd_, IDC_C3), {"Open", "In Progress", "Closed"});
    if (name == "evidence") {
        comboAdd(GetDlgItem(hwnd_, IDC_C1), {"Laptop", "USB", "Phone", "Disk Image", "Log File", "Other"});
    }
    if (name == "users") {
        comboAdd(GetDlgItem(hwnd_, IDC_C1), {"admin", "investigator", "viewer"});
    }
    setText(output_, "Module: " + name + "\nF1-F8 are form fields. Select a row to load it.");
    refresh();
}

void GuiApp::loadSelected() {
    const int row = listSelected(list_);
    if (row < 0) return;
    for (int i = 0; i < 8; ++i) {
        setField(IDC_F1 + i, listCell(list_, row, i));
    }
}

void GuiApp::refresh() {
    try {
        std::vector<std::string> headers;
        std::vector<std::vector<std::string>> rows;
        if (module_ == "cases") {
            headers = {"ID", "Title", "Type", "Priority", "Status", "Opened", "Deadline", "Investigator"};
            auto stmt = db_->prepare(
                "SELECT c.case_id, c.title, c.type, c.priority, c.status, c.opened_date, "
                "c.deadline, i.name FROM cases c JOIN investigators i "
                "ON c.assigned_investigator_id = i.investigator_id WHERE c.is_active = 1 ORDER BY c.case_id");
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            while (rs->next()) {
                rows.push_back({cell(*rs, "case_id"), rs->getString("title"), rs->getString("type"),
                                rs->getString("priority"), rs->getString("status"),
                                cell(*rs, "opened_date"), cell(*rs, "deadline"), rs->getString("name")});
            }
        } else if (module_ == "evidence") {
            headers = {"ID", "Case", "Description", "Type", "SHA-256", "Size", "Verified", "Active"};
            auto stmt = db_->prepare(
                "SELECT evidence_id, case_id, description, type, sha256_hash, size_mb, verified, is_active "
                "FROM evidence WHERE is_active = 1 ORDER BY evidence_id");
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            while (rs->next()) {
                const std::string desc = maskSensitive() ? "********" : rs->getString("description");
                rows.push_back({cell(*rs, "evidence_id"), cell(*rs, "case_id"), desc,
                                rs->getString("type"), rs->getString("sha256_hash").substr(0, 16),
                                cell(*rs, "size_mb"), yesNo(*rs, "verified"), yesNo(*rs, "is_active")});
            }
        } else if (module_ == "investigators") {
            headers = {"ID", "Name", "Role", "Specialty", "Department"};
            auto stmt = db_->prepare(
                "SELECT investigator_id, name, role, specialty, department FROM investigators ORDER BY investigator_id");
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            while (rs->next()) {
                rows.push_back({cell(*rs, "investigator_id"), rs->getString("name"), rs->getString("role"),
                                rs->getString("specialty"), rs->getString("department")});
            }
        } else if (module_ == "custody") {
            headers = {"ID", "Evidence", "From", "To", "When", "Reason"};
            auto stmt = db_->prepare(
                "SELECT transfer_id, evidence_id, from_person, to_person, transfer_datetime, reason "
                "FROM custody_transfers ORDER BY transfer_id");
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            while (rs->next()) {
                rows.push_back({cell(*rs, "transfer_id"), cell(*rs, "evidence_id"), rs->getString("from_person"),
                                rs->getString("to_person"), cell(*rs, "transfer_datetime"), rs->getString("reason")});
            }
        } else if (module_ == "users") {
            headers = {"ID", "Username", "Role", "Fails", "Locked", "Active"};
            auto stmt = db_->prepare(
                "SELECT user_id, username, role, failed_attempts, locked_until, is_active FROM users ORDER BY user_id");
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            while (rs->next()) {
                rows.push_back({cell(*rs, "user_id"), rs->getString("username"), rs->getString("role"),
                                cell(*rs, "failed_attempts"), cell(*rs, "locked_until"), yesNo(*rs, "is_active")});
            }
        } else if (module_ == "audit") {
            doReport(6);
            return;
        } else if (module_ == "backup") {
            setText(output_, "Click Backup to export all tables as CSV + SQL.");
            return;
        } else if (module_ == "analysis") {
            doAnalysis(1);
            return;
        } else if (module_ == "reports") {
            doReport(session_.role == Role::Viewer ? 1 : 1);
            return;
        }
        fillList(headers, rows);
    } catch (const std::exception& ex) {
        Logger::instance().error(std::string("gui refresh: ") + ex.what());
        errorBox(hwnd_, "Operation failed.");
    }
}

void GuiApp::doCreate() {
    try {
        if (module_ == "cases") {
            if (!requireRole("cases")) { errorBox(hwnd_, "Access denied."); return; }
            const std::string title = field(IDC_F1);
            const std::string opened = field(IDC_F2);
            const std::string deadline = field(IDC_F3);
            const int inv = toInt(field(IDC_F4));
            if (title.size() < 3 || !Validation::isIsoDate(opened) || !Validation::isIsoDate(deadline) || inv < 1) {
                errorBox(hwnd_, "Need title, YYYY-MM-DD dates, investigator ID.");
                return;
            }
            if (deadline < opened) { errorBox(hwnd_, "Deadline cannot be before opened date."); return; }
            auto stmt = db_->prepare(
                "INSERT INTO cases (title, type, priority, status, opened_date, deadline, closed_date, "
                "assigned_investigator_id, is_active) VALUES (?, ?, ?, 'Open', ?, ?, NULL, ?, 1)");
            stmt->setString(1, title);
            stmt->setString(2, comboText(GetDlgItem(hwnd_, IDC_C1)));
            stmt->setString(3, comboText(GetDlgItem(hwnd_, IDC_C2)));
            stmt->setString(4, opened);
            stmt->setString(5, deadline);
            stmt->setInt(6, inv);
            stmt->executeUpdate();
            audit_->write(session_.userId, "CREATE", "case title=" + title, true);
        } else if (module_ == "evidence") {
            if (!requireRole("evidence")) { errorBox(hwnd_, "Access denied."); return; }
            const int caseId = toInt(field(IDC_F1));
            const std::string desc = field(IDC_F2);
            const std::string collected = field(IDC_F3);
            const std::string loc = field(IDC_F4);
            const std::string source = field(IDC_F5);
            if (caseId < 1 || desc.size() < 3 || !Validation::isIsoDate(collected) || loc.size() < 2 || source.empty()) {
                errorBox(hwnd_, "F1 case ID, F2 description, F3 date, F4 location, F5 file path or text.");
                return;
            }
            double sizeMb = 0;
            std::string hash = HashUtil::sha256File(source, sizeMb);
            if (hash.empty()) hash = HashUtil::sha256Text(source, sizeMb);
            if (hash.empty()) { errorBox(hwnd_, "Could not hash source."); return; }
            auto stmt = db_->prepare(
                "INSERT INTO evidence (case_id, description, type, sha256_hash, size_mb, collected_date, "
                "storage_location, verified, is_active) VALUES (?, ?, ?, ?, ?, ?, ?, 0, 1)");
            stmt->setInt(1, caseId);
            stmt->setString(2, desc);
            stmt->setString(3, comboText(GetDlgItem(hwnd_, IDC_C1)));
            stmt->setString(4, hash);
            stmt->setDouble(5, sizeMb);
            stmt->setString(6, collected);
            stmt->setString(7, loc);
            stmt->executeUpdate();
            audit_->write(session_.userId, "CREATE", "evidence case_id=" + std::to_string(caseId), true);
            infoBox(hwnd_, "SHA-256: " + hash);
        } else if (module_ == "investigators") {
            if (session_.role != Role::Admin) { errorBox(hwnd_, "Access denied."); return; }
            auto stmt = db_->prepare(
                "INSERT INTO investigators (name, role, specialty, department) VALUES (?, ?, ?, ?)");
            stmt->setString(1, field(IDC_F1));
            stmt->setString(2, field(IDC_F2));
            stmt->setString(3, field(IDC_F3));
            stmt->setString(4, field(IDC_F4));
            stmt->executeUpdate();
            audit_->write(session_.userId, "CREATE", "investigator name=" + field(IDC_F1), true);
        } else if (module_ == "custody") {
            if (!requireRole("custody")) { errorBox(hwnd_, "Access denied."); return; }
            const int evid = toInt(field(IDC_F1));
            auto chk = db_->prepare("SELECT is_active FROM evidence WHERE evidence_id = ?");
            chk->setInt(1, evid);
            std::unique_ptr<sql::ResultSet> rs(chk->executeQuery());
            if (!rs->next() || !rs->getBoolean("is_active")) {
                errorBox(hwnd_, "Cannot add a transfer for missing or inactive evidence.");
                return;
            }
            auto stmt = db_->prepare(
                "INSERT INTO custody_transfers (evidence_id, from_person, to_person, transfer_datetime, reason) "
                "VALUES (?, ?, ?, IF(? = '', NOW(), ?), ?)");
            stmt->setInt(1, evid);
            stmt->setString(2, field(IDC_F2));
            stmt->setString(3, field(IDC_F3));
            stmt->setString(4, field(IDC_F4));
            stmt->setString(5, field(IDC_F4));
            stmt->setString(6, field(IDC_F5));
            stmt->executeUpdate();
            audit_->write(session_.userId, "CREATE", "custody evidence_id=" + std::to_string(evid), true);
        } else if (module_ == "users") {
            if (session_.role != Role::Admin) { errorBox(hwnd_, "Access denied."); return; }
            const std::string username = field(IDC_F1);
            const std::string password = field(IDC_F2);
            if (!Validation::isUsername(username) || !users_->passwordMeetsPolicy(password)) {
                errorBox(hwnd_, "Username 3-32 alnum/_ ; password 8+ with digit and symbol.");
                return;
            }
            const std::string salt = users_->createSalt();
            const std::string hash = users_->hashWithSalt(password, salt);
            auto stmt = db_->prepare(
                "INSERT INTO users (username, password_hash, salt, role, failed_attempts, locked_until, is_active) "
                "VALUES (?, ?, ?, ?, 0, NULL, 1)");
            stmt->setString(1, username);
            stmt->setString(2, hash);
            stmt->setString(3, salt);
            stmt->setString(4, comboText(GetDlgItem(hwnd_, IDC_C1)));
            stmt->executeUpdate();
            audit_->write(session_.userId, "CREATE", "user username=" + username, true);
        } else {
            errorBox(hwnd_, "Create is not used in this module.");
            return;
        }
        infoBox(hwnd_, "Created.");
        refresh();
    } catch (const std::exception& ex) {
        Logger::instance().error(std::string("gui create: ") + ex.what());
        errorBox(hwnd_, "Operation failed.");
    }
}

void GuiApp::doUpdate() {
    try {
        if (module_ == "cases") {
            if (!requireRole("cases")) { errorBox(hwnd_, "Access denied."); return; }
            const int id = toInt(field(IDC_F1));
            const std::string status = comboText(GetDlgItem(hwnd_, IDC_C3));
            if (status == "Closed") {
                auto cnt = db_->prepare(
                    "SELECT COUNT(*) AS n FROM evidence WHERE case_id = ? AND is_active = 1 AND verified = 0");
                cnt->setInt(1, id);
                std::unique_ptr<sql::ResultSet> rs(cnt->executeQuery());
                rs->next();
                const int pending = rs->getInt("n");
                if (pending > 0) {
                    if (session_.role != Role::Admin) {
                        errorBox(hwnd_, "Unverified evidence. Only admin can override.");
                        return;
                    }
                    if (!askBox(hwnd_, "Admin override and close anyway?")) return;
                    audit_->write(session_.userId, "CASE_CLOSE_OVERRIDE",
                                  "case_id=" + std::to_string(id), true);
                }
            }
            auto stmt = db_->prepare(
                "UPDATE cases SET title = ?, type = ?, priority = ?, status = ?, deadline = ?, "
                "assigned_investigator_id = ?, closed_date = CASE WHEN ? = 'Closed' THEN CURDATE() ELSE NULL END "
                "WHERE case_id = ? AND is_active = 1");
            stmt->setString(1, field(IDC_F2));
            stmt->setString(2, comboText(GetDlgItem(hwnd_, IDC_C1)));
            stmt->setString(3, comboText(GetDlgItem(hwnd_, IDC_C2)));
            stmt->setString(4, status);
            stmt->setString(5, field(IDC_F3));
            stmt->setInt(6, toInt(field(IDC_F4), 1));
            stmt->setString(7, status);
            stmt->setInt(8, id);
            stmt->executeUpdate();
            audit_->write(session_.userId, "UPDATE", "case_id=" + std::to_string(id), true);
        } else if (module_ == "evidence") {
            if (!requireRole("evidence")) { errorBox(hwnd_, "Access denied."); return; }
            auto stmt = db_->prepare(
                "UPDATE evidence SET description = ?, type = ?, storage_location = ?, collected_date = ? "
                "WHERE evidence_id = ? AND is_active = 1");
            stmt->setString(1, field(IDC_F2));
            stmt->setString(2, comboText(GetDlgItem(hwnd_, IDC_C1)));
            stmt->setString(3, field(IDC_F4));
            stmt->setString(4, field(IDC_F3));
            stmt->setInt(5, toInt(field(IDC_F1)));
            stmt->executeUpdate();
            audit_->write(session_.userId, "UPDATE", "evidence_id=" + field(IDC_F1), true);
        } else if (module_ == "investigators") {
            if (session_.role != Role::Admin) { errorBox(hwnd_, "Access denied."); return; }
            auto stmt = db_->prepare(
                "UPDATE investigators SET name = ?, role = ?, specialty = ?, department = ? WHERE investigator_id = ?");
            stmt->setString(1, field(IDC_F2));
            stmt->setString(2, field(IDC_F3));
            stmt->setString(3, field(IDC_F4));
            stmt->setString(4, field(IDC_F5));
            stmt->setInt(5, toInt(field(IDC_F1)));
            stmt->executeUpdate();
            audit_->write(session_.userId, "UPDATE", "investigator_id=" + field(IDC_F1), true);
        } else if (module_ == "users") {
            if (session_.role != Role::Admin) { errorBox(hwnd_, "Access denied."); return; }
            auto stmt = db_->prepare("UPDATE users SET role = ? WHERE user_id = ?");
            stmt->setString(1, comboText(GetDlgItem(hwnd_, IDC_C1)));
            stmt->setInt(2, toInt(field(IDC_F1)));
            stmt->executeUpdate();
            audit_->write(session_.userId, "UPDATE", "user_id=" + field(IDC_F1), true);
        } else if (module_ == "custody") {
            errorBox(hwnd_, "Custody is append-only. Update is not allowed.");
            return;
        }
        infoBox(hwnd_, "Updated.");
        refresh();
    } catch (const std::exception& ex) {
        Logger::instance().error(std::string("gui update: ") + ex.what());
        errorBox(hwnd_, "Operation failed.");
    }
}

void GuiApp::doDelete() {
    try {
        if (module_ == "cases") {
            if (!requireRole("cases")) { errorBox(hwnd_, "Access denied."); return; }
            if (!askBox(hwnd_, "Soft-delete this case?")) return;
            auto stmt = db_->prepare("UPDATE cases SET is_active = 0 WHERE case_id = ?");
            stmt->setInt(1, toInt(field(IDC_F1)));
            stmt->executeUpdate();
            audit_->write(session_.userId, "DELETE", "case_id=" + field(IDC_F1), true);
        } else if (module_ == "evidence") {
            if (!requireRole("evidence")) { errorBox(hwnd_, "Access denied."); return; }
            if (!askBox(hwnd_, "Soft-delete this evidence?")) return;
            auto stmt = db_->prepare("UPDATE evidence SET is_active = 0 WHERE evidence_id = ?");
            stmt->setInt(1, toInt(field(IDC_F1)));
            stmt->executeUpdate();
            audit_->write(session_.userId, "DELETE", "evidence_id=" + field(IDC_F1), true);
        } else if (module_ == "users") {
            if (session_.role != Role::Admin) { errorBox(hwnd_, "Access denied."); return; }
            auto stmt = db_->prepare("UPDATE users SET is_active = 0 WHERE user_id = ?");
            stmt->setInt(1, toInt(field(IDC_F1)));
            stmt->executeUpdate();
            audit_->write(session_.userId, "DELETE", "user_id=" + field(IDC_F1), true);
        } else if (module_ == "investigators") {
            if (session_.role != Role::Admin) { errorBox(hwnd_, "Access denied."); return; }
            auto stmt = db_->prepare("DELETE FROM investigators WHERE investigator_id = ?");
            stmt->setInt(1, toInt(field(IDC_F1)));
            stmt->executeUpdate();
            audit_->write(session_.userId, "DELETE", "investigator_id=" + field(IDC_F1), true);
        } else if (module_ == "custody") {
            errorBox(hwnd_, "Custody is append-only. Delete is not allowed.");
            return;
        }
        infoBox(hwnd_, "Deleted.");
        refresh();
    } catch (const std::exception& ex) {
        Logger::instance().error(std::string("gui delete: ") + ex.what());
        errorBox(hwnd_, "Operation failed.");
    }
}

void GuiApp::doSearch() {
    try {
        const std::string q = field(IDC_F1);
        if (module_ == "cases") {
            auto stmt = db_->prepare(
                "SELECT c.case_id, c.title, c.type, c.priority, c.status, c.is_active, i.name "
                "FROM cases c JOIN investigators i ON c.assigned_investigator_id = i.investigator_id "
                "WHERE (? = '' OR c.title LIKE CONCAT('%', ?, '%') OR c.status = ? OR c.type = ? "
                "OR CAST(c.case_id AS CHAR) = ?) ORDER BY c.case_id");
            for (int i = 1; i <= 5; ++i) stmt->setString(i, q);
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            std::vector<std::vector<std::string>> rows;
            while (rs->next()) {
                rows.push_back({cell(*rs, "case_id"), rs->getString("title"), rs->getString("type"),
                                rs->getString("priority"), rs->getString("status"),
                                yesNo(*rs, "is_active"), rs->getString("name")});
            }
            fillList({"ID", "Title", "Type", "Priority", "Status", "Active", "Investigator"}, rows);
        } else if (module_ == "evidence") {
            auto stmt = db_->prepare(
                "SELECT evidence_id, case_id, description, type, sha256_hash, verified "
                "FROM evidence WHERE (? = '' OR CAST(evidence_id AS CHAR) = ? OR CAST(case_id AS CHAR) = ? "
                "OR type = ? OR sha256_hash LIKE CONCAT(?, '%') OR description LIKE CONCAT('%', ?, '%'))");
            for (int i = 1; i <= 6; ++i) stmt->setString(i, q);
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            std::vector<std::vector<std::string>> rows;
            while (rs->next()) {
                rows.push_back({cell(*rs, "evidence_id"), cell(*rs, "case_id"),
                                maskSensitive() ? "********" : rs->getString("description"),
                                rs->getString("type"), rs->getString("sha256_hash").substr(0, 16),
                                yesNo(*rs, "verified")});
            }
            fillList({"ID", "Case", "Description", "Type", "SHA-256", "Verified"}, rows);
        } else {
            refresh();
        }
    } catch (const std::exception& ex) {
        Logger::instance().error(std::string("gui search: ") + ex.what());
        errorBox(hwnd_, "Operation failed.");
    }
}

void GuiApp::doVerify() {
    if (!requireRole("evidence")) { errorBox(hwnd_, "Access denied."); return; }
    try {
        const int id = toInt(field(IDC_F1));
        auto load = db_->prepare("SELECT sha256_hash, is_active FROM evidence WHERE evidence_id = ?");
        load->setInt(1, id);
        std::unique_ptr<sql::ResultSet> rs(load->executeQuery());
        if (!rs->next() || !rs->getBoolean("is_active")) {
            errorBox(hwnd_, "Evidence not found or inactive.");
            return;
        }
        const std::string stored = rs->getString("sha256_hash");
        const std::string source = field(IDC_F5);
        double sizeMb = 0;
        std::string hash = HashUtil::sha256File(source, sizeMb);
        if (hash.empty()) hash = HashUtil::sha256Text(source, sizeMb);
        const bool match = (hash == stored);
        auto upd = db_->prepare("UPDATE evidence SET verified = ? WHERE evidence_id = ?");
        upd->setInt(1, match ? 1 : 0);
        upd->setInt(2, id);
        upd->executeUpdate();
        audit_->write(session_.userId, "HASH_VERIFY",
                      "evidence_id=" + std::to_string(id) + (match ? " match" : " mismatch"), match);
        infoBox(hwnd_, match ? "Hash matches. Verified." : "Hash mismatch. Unverified.");
        refresh();
    } catch (const std::exception& ex) {
        Logger::instance().error(std::string("gui verify: ") + ex.what());
        errorBox(hwnd_, "Operation failed.");
    }
}

void GuiApp::doAnalysis(int which) {
    if (!requireRole("analysis")) { errorBox(hwnd_, "Access denied."); return; }
    try {
        std::ostringstream out;
        std::vector<std::string> headers;
        std::vector<std::vector<std::string>> rows;
        if (which == 1) {
            out << "AGGREGATION: COUNT/AVG/SUM of cases and evidence.\n";
            auto stmt = db_->prepare(
                "SELECT type, COUNT(*) AS n, ROUND(AVG(DATEDIFF(IFNULL(closed_date, CURDATE()), opened_date)),1) AS avg_days "
                "FROM cases WHERE is_active = 1 GROUP BY type");
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            headers = {"Type", "Cases", "Avg days"};
            while (rs->next()) rows.push_back({rs->getString("type"), cell(*rs, "n"), cell(*rs, "avg_days")});
        } else if (which == 2) {
            out << "GROUPING: cases per month and investigators with 3+ open cases.\n";
            auto stmt = db_->prepare(
                "SELECT DATE_FORMAT(opened_date,'%Y-%m') AS month, COUNT(*) AS n FROM cases "
                "WHERE is_active = 1 GROUP BY DATE_FORMAT(opened_date,'%Y-%m') ORDER BY month");
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            headers = {"Month", "Cases"};
            while (rs->next()) rows.push_back({rs->getString("month"), cell(*rs, "n")});
        } else if (which == 3) {
            out << "JOIN: cases + investigator + evidence.\n";
            auto stmt = db_->prepare(
                "SELECT c.case_id, c.title, i.name, e.evidence_id, e.type FROM cases c "
                "JOIN investigators i ON c.assigned_investigator_id = i.investigator_id "
                "LEFT JOIN evidence e ON e.case_id = c.case_id AND e.is_active = 1 "
                "WHERE c.is_active = 1 ORDER BY c.case_id LIMIT 80");
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            headers = {"Case", "Title", "Investigator", "Evidence", "E-Type"};
            while (rs->next()) rows.push_back({cell(*rs, "case_id"), rs->getString("title"), rs->getString("name"),
                                              cell(*rs, "evidence_id"), cell(*rs, "type")});
        } else if (which == 4) {
            out << "SUBQUERY: cases with no evidence; evidence never transferred.\n";
            auto stmt = db_->prepare(
                "SELECT case_id, title, status FROM cases WHERE is_active = 1 AND case_id NOT IN "
                "(SELECT DISTINCT case_id FROM evidence WHERE is_active = 1)");
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            headers = {"ID", "Title", "Status"};
            while (rs->next()) rows.push_back({cell(*rs, "case_id"), rs->getString("title"), rs->getString("status")});
        } else if (which == 5) {
            out << "UNARY: days remaining; negative means overdue. NOT verified evidence.\n";
            auto stmt = db_->prepare(
                "SELECT case_id, title, deadline, "
                "CASE WHEN DATEDIFF(deadline, CURDATE()) < 0 THEN -ABS(DATEDIFF(deadline, CURDATE())) "
                "ELSE DATEDIFF(deadline, CURDATE()) END AS days_remaining "
                "FROM cases WHERE is_active = 1 AND status <> 'Closed' ORDER BY days_remaining");
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            headers = {"ID", "Title", "Deadline", "Days"};
            while (rs->next()) rows.push_back({cell(*rs, "case_id"), rs->getString("title"),
                                              cell(*rs, "deadline"), cell(*rs, "days_remaining")});
        } else {
            out << "DECISION SUPPORT: workload score Low1 Medium2 High3 Critical5; recommend lowest.\n";
            auto stmt = db_->prepare(
                "SELECT i.name, COUNT(c.case_id) AS open_cases, "
                "SUM(CASE c.priority WHEN 'Low' THEN 1 WHEN 'Medium' THEN 2 WHEN 'High' THEN 3 "
                "WHEN 'Critical' THEN 5 ELSE 0 END) AS score FROM investigators i "
                "LEFT JOIN cases c ON c.assigned_investigator_id = i.investigator_id "
                "AND c.is_active = 1 AND c.status IN ('Open','In Progress') "
                "GROUP BY i.investigator_id, i.name ORDER BY score ASC");
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            headers = {"Name", "Open", "Score"};
            bool first = true;
            while (rs->next()) {
                rows.push_back({rs->getString("name"), cell(*rs, "open_cases"), cell(*rs, "score")});
                if (first) {
                    out << "Recommend next investigator: " << rs->getString("name") << "\n";
                    first = false;
                }
            }
        }
        lastReport_ = out.str();
        setText(output_, lastReport_);
        fillList(headers, rows);
    } catch (const std::exception& ex) {
        Logger::instance().error(std::string("gui analysis: ") + ex.what());
        errorBox(hwnd_, "Operation failed.");
    }
}

void GuiApp::doReport(int which) {
    if (session_.role == Role::Viewer && which != 1 && which != 4 && which != 5) {
        errorBox(hwnd_, "Viewer can only open reports 1, 4 and 5.");
        return;
    }
    if (!requireRole("reports") && session_.role != Role::Viewer) {
        errorBox(hwnd_, "Access denied.");
        return;
    }
    if (which == 6 && session_.role != Role::Admin) {
        errorBox(hwnd_, "Audit report is admin only.");
        return;
    }
    try {
        std::ostringstream out;
        std::vector<std::string> headers;
        std::vector<std::vector<std::string>> rows;
        if (which == 1) {
            out << "Opened vs closed per month.\n";
            auto stmt = db_->prepare(
                "SELECT DATE_FORMAT(opened_date,'%Y-%m') AS month, COUNT(*) AS opened_n "
                "FROM cases WHERE is_active = 1 GROUP BY DATE_FORMAT(opened_date,'%Y-%m') ORDER BY month");
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            headers = {"Month", "Opened"};
            while (rs->next()) rows.push_back({rs->getString("month"), cell(*rs, "opened_n")});
        } else if (which == 2) {
            out << "Top investigators, overdue cases, unverified evidence.\n";
            auto stmt = db_->prepare(
                "SELECT i.name, COUNT(c.case_id) AS n FROM investigators i JOIN cases c "
                "ON c.assigned_investigator_id = i.investigator_id WHERE c.is_active = 1 "
                "AND c.status IN ('Open','In Progress') GROUP BY i.investigator_id, i.name "
                "ORDER BY n DESC LIMIT 5");
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            headers = {"Name", "Open cases"};
            while (rs->next()) rows.push_back({rs->getString("name"), cell(*rs, "n")});
        } else if (which == 3) {
            const int evid = toInt(field(IDC_F1), 1);
            out << "Custody timeline for evidence " << evid << ".\n";
            auto stmt = db_->prepare(
                "SELECT transfer_datetime, from_person, to_person, reason FROM custody_transfers "
                "WHERE evidence_id = ? ORDER BY transfer_datetime");
            stmt->setInt(1, evid);
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            headers = {"When", "From", "To", "Reason"};
            while (rs->next()) rows.push_back({cell(*rs, "transfer_datetime"), rs->getString("from_person"),
                                              rs->getString("to_person"), rs->getString("reason")});
        } else if (which == 4) {
            out << "Cases per type (bar scaled to max).\n";
            auto stmt = db_->prepare(
                "SELECT type, COUNT(*) AS n FROM cases WHERE is_active = 1 GROUP BY type ORDER BY n DESC");
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            int maxV = 0;
            std::vector<std::pair<std::string, int>> items;
            while (rs->next()) {
                const int n = rs->getInt("n");
                items.push_back({rs->getString("type"), n});
                if (n > maxV) maxV = n;
            }
            headers = {"Type", "Bar", "Count"};
            for (const auto& item : items) {
                rows.push_back({item.first, bar(item.second, maxV, 24), std::to_string(item.second)});
                out << item.first << " |" << bar(item.second, maxV, 24) << "| " << item.second << "\n";
            }
        } else if (which == 5) {
            out << "Monthly trend.\n";
            auto stmt = db_->prepare(
                "SELECT DATE_FORMAT(opened_date,'%Y-%m') AS month, COUNT(*) AS n FROM cases "
                "WHERE is_active = 1 GROUP BY DATE_FORMAT(opened_date,'%Y-%m') ORDER BY month");
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            int maxV = 0;
            std::vector<std::pair<std::string, int>> items;
            while (rs->next()) {
                const int n = rs->getInt("n");
                items.push_back({rs->getString("month"), n});
                if (n > maxV) maxV = n;
            }
            headers = {"Month", "Bar", "Count"};
            for (const auto& item : items) {
                rows.push_back({item.first, bar(item.second, maxV, 24), std::to_string(item.second)});
                out << item.first << " |" << bar(item.second, maxV, 24) << "| " << item.second << "\n";
            }
        } else {
            out << "Audit log.\n";
            auto stmt = db_->prepare(
                "SELECT timestamp, IFNULL(u.username,'-') AS username, a.action, a.target, a.result "
                "FROM audit_log a LEFT JOIN users u ON u.user_id = a.user_id ORDER BY a.timestamp DESC LIMIT 100");
            std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
            headers = {"Time", "User", "Action", "Target", "Result"};
            while (rs->next()) rows.push_back({cell(*rs, "timestamp"), rs->getString("username"),
                                              rs->getString("action"), rs->getString("target"),
                                              rs->getString("result")});
        }
        lastReport_ = out.str();
        setText(output_, lastReport_);
        fillList(headers, rows);
    } catch (const std::exception& ex) {
        Logger::instance().error(std::string("gui report: ") + ex.what());
        errorBox(hwnd_, "Operation failed.");
    }
}

void GuiApp::doExport() {
    try {
        std::filesystem::create_directories("reports");
        const std::string path = "reports/gui_export.txt";
        std::ofstream file(path);
        file << lastReport_ << "\n";
        const int n = ListView_GetItemCount(list_);
        const int cols = Header_GetItemCount(ListView_GetHeader(list_));
        for (int r = 0; r < n; ++r) {
            for (int c = 0; c < cols; ++c) {
                if (c) file << ",";
                file << listCell(list_, r, c);
            }
            file << "\n";
        }
        audit_->write(session_.userId, "REPORT_EXPORT", path, true);
        infoBox(hwnd_, "Wrote " + path);
    } catch (const std::exception& ex) {
        Logger::instance().error(std::string("gui export: ") + ex.what());
        errorBox(hwnd_, "Operation failed.");
    }
}

void GuiApp::doBackup() {
    if (session_.role != Role::Admin) { errorBox(hwnd_, "Access denied."); return; }
    try {
        const std::string folder = backup_->exportDatabase(session_);
        infoBox(hwnd_, "Backup written to " + folder);
        setText(output_, "Backup: " + folder);
    } catch (const std::exception& ex) {
        Logger::instance().error(std::string("gui backup: ") + ex.what());
        errorBox(hwnd_, "Operation failed.");
    }
}

#endif
