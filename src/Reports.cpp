#include "Reports.h"
#include "Logger.h"

#include <cppconn/exception.h>
#include <cppconn/resultset.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {
std::string cellOf(sql::ResultSet& rs, const std::string& col) {
    if (rs.isNull(col)) {
        return "-";
    }
    return rs.getString(col);
}

std::string nowStamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return out.str();
}

std::string nowHuman() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

std::string todayIso() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d");
    return out.str();
}

int lastDayOfMonth(int year, int month) {
    static const int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2) {
        const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    if (month < 1 || month > 12) {
        return 28;
    }
    return days[month];
}

std::string csvEscape(const std::string& value) {
    bool quote = false;
    for (char ch : value) {
        if (ch == ',' || ch == '"' || ch == '\n' || ch == '\r') {
            quote = true;
            break;
        }
    }
    if (!quote) {
        return value;
    }
    std::string out = "\"";
    for (char ch : value) {
        if (ch == '"') {
            out += "\"\"";
        } else {
            out += ch;
        }
    }
    out += '"';
    return out;
}

std::string padRight(const std::string& value, std::size_t width) {
    if (value.size() >= width) {
        return value;
    }
    return value + std::string(width - value.size(), ' ');
}

void fail(ConsoleUI& ui, const std::exception& ex) {
    Logger::instance().error(std::string("reports: ") + ex.what());
    ui.printError("Operation failed.");
}

std::string tableText(const std::vector<std::string>& headers,
                      const std::vector<std::vector<std::string>>& rows) {
    if (headers.empty()) {
        return {};
    }
    std::vector<std::size_t> widths(headers.size(), 0);
    for (std::size_t i = 0; i < headers.size(); ++i) {
        widths[i] = headers[i].size();
    }
    for (const auto& row : rows) {
        for (std::size_t i = 0; i < row.size() && i < widths.size(); ++i) {
            if (row[i].size() > widths[i]) {
                widths[i] = row[i].size();
            }
        }
    }
    std::ostringstream out;
    auto sep = [&]() {
        out << "+";
        for (std::size_t w : widths) {
            out << std::string(w + 2, '-') << "+";
        }
        out << "\n";
    };
    sep();
    out << "|";
    for (std::size_t i = 0; i < headers.size(); ++i) {
        out << " " << headers[i] << std::string(widths[i] - headers[i].size(), ' ') << " |";
    }
    out << "\n";
    sep();
    for (const auto& row : rows) {
        out << "|";
        for (std::size_t i = 0; i < headers.size(); ++i) {
            const std::string cell = i < row.size() ? row[i] : "";
            out << " " << cell << std::string(widths[i] - cell.size(), ' ') << " |";
        }
        out << "\n";
    }
    sep();
    return out.str();
}
}

Reports::Reports(DatabaseConnection& db, AuditLog& audit, ConsoleUI& ui)
    : db_(db), audit_(audit), ui_(ui) {}

void Reports::showMenu(const Session& session) {
    if (!session.authenticated ||
        (session.role != Role::Admin && session.role != Role::Investigator)) {
        ui_.printError("Access denied.");
        ui_.pause();
        return;
    }
    bool back = false;
    while (!back) {
        std::vector<std::string> options = {
            "Case status summary (opened vs closed per month)",
            "Summary lists (busiest, overdue, unverified)",
            "Chain-of-custody timeline",
            "Bar chart: cases per type",
            "Trend: cases per month and priority share",
        };
        if (session.role == Role::Admin) {
            options.push_back("Audit log report");
        }
        options.push_back("Back");
        const int choice = ui_.showMenu("Report Generator", options);
        const int backIndex = static_cast<int>(options.size());
        if (choice == backIndex) {
            back = true;
            continue;
        }
        switch (choice) {
            case 1: caseStatus(session); break;
            case 2: summaryLists(session); break;
            case 3: custodyTimeline(session); break;
            case 4: typeChart(session); break;
            case 5: monthlyTrend(session); break;
            case 6:
                if (session.role == Role::Admin) {
                    auditReport(session);
                }
                break;
            default: break;
        }
    }
}

void Reports::showSummary(const Session& session) {
    if (!session.authenticated || session.role != Role::Viewer) {
        ui_.printError("Access denied.");
        ui_.pause();
        return;
    }
    bool back = false;
    while (!back) {
        const int choice = ui_.showMenu("Summary Reports", {
            "Case status summary (opened vs closed per month)",
            "Bar chart: cases per type",
            "Trend: cases per month and priority share",
            "Back"
        });
        switch (choice) {
            case 1: caseStatus(session); break;
            case 2: typeChart(session); break;
            case 3: monthlyTrend(session); break;
            default: back = true; break;
        }
    }
}

Reports::DateFilter Reports::allDates() const {
    return {"2000-01-01", "2100-12-31", "all dates"};
}

Reports::DateFilter Reports::askDateFilter(bool allowAll) {
    std::vector<std::string> options;
    if (allowAll) {
        options.push_back("All dates");
    }
    options.push_back("Today");
    options.push_back("This week");
    options.push_back("This month");
    options.push_back("Custom range");
    const int choice = ui_.pick("Date filter: ", options);
    if (choice == 0) {
        return allDates();
    }
    int index = choice;
    if (!allowAll) {
        index += 1;
    }
    if (allowAll && index == 1) {
        return allDates();
    }
    if (index == 2) {
        const std::string day = todayIso();
        return {day, day, "today " + day};
    }
    if (index == 3) {
        const std::string day = todayIso();
        std::tm tm{};
        int year = 0;
        int month = 0;
        int d = 0;
        std::sscanf(day.c_str(), "%d-%d-%d", &year, &month, &d);
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = d;
        tm.tm_hour = 12;
        std::mktime(&tm);
        int wday = tm.tm_wday;
        if (wday == 0) wday = 7;
        tm.tm_mday -= (wday - 1);
        std::mktime(&tm);
        std::ostringstream from;
        from << std::put_time(&tm, "%Y-%m-%d");
        return {from.str(), day, "this week " + from.str() + " to " + day};
    }
    if (index == 4) {
        const std::string day = todayIso();
        int year = 0;
        int month = 0;
        int dummy = 0;
        std::sscanf(day.c_str(), "%d-%d-%d", &year, &month, &dummy);
        std::ostringstream from;
        from << std::setfill('0') << std::setw(4) << year << "-"
             << std::setw(2) << month << "-01";
        std::ostringstream to;
        to << std::setfill('0') << std::setw(4) << year << "-"
           << std::setw(2) << month << "-" << std::setw(2) << lastDayOfMonth(year, month);
        return {from.str(), to.str(), "monthly " + from.str().substr(0, 7)};
    }
    const std::string from = ui_.readDate("From (YYYY-MM-DD): ", false);
    std::string to = ui_.readDate("To (YYYY-MM-DD): ", false);
    if (to < from) {
        ui_.printError("End date was before start date; swapping.");
        const std::string tmp = from;
        return {to, tmp, "custom " + to + " to " + tmp};
    }
    return {from, to, "custom " + from + " to " + to};
}

std::string Reports::bar(int value, int maxValue, int width) const {
    if (maxValue <= 0 || value <= 0) {
        return std::string(width, ' ');
    }
    int filled = (value * width) / maxValue;
    if (value > 0 && filled == 0) {
        filled = 1;
    }
    if (filled > width) {
        filled = width;
    }
    return std::string(static_cast<std::size_t>(filled), '#') +
           std::string(static_cast<std::size_t>(width - filled), ' ');
}

void Reports::printReport(const ReportData& data) const {
    ui_.printHeader(data.title);
    for (const auto& note : data.notes) {
        std::cout << note << "\n";
    }
    for (const auto& section : data.sections) {
        std::cout << "\n" << section.heading << "\n";
        if (!section.headers.empty()) {
            if (section.rows.empty()) {
                std::cout << "No rows.\n";
            } else {
                ui_.printTable(section.headers, section.rows);
            }
        }
        for (const auto& line : section.lines) {
            std::cout << line << "\n";
        }
    }
}

void Reports::writeTxt(const std::string& path, const ReportData& data, const DateFilter& filter) const {
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("cannot write " + path);
    }
    file << data.title << "\n";
    file << "Generated: " << nowHuman() << "\n";
    file << "Filter: " << filter.label << " (" << filter.from << " to " << filter.to << ")\n";
    for (const auto& note : data.notes) {
        file << note << "\n";
    }
    for (const auto& section : data.sections) {
        file << "\n" << section.heading << "\n";
        if (!section.headers.empty()) {
            if (section.rows.empty()) {
                file << "No rows.\n";
            } else {
                file << tableText(section.headers, section.rows);
            }
        }
        for (const auto& line : section.lines) {
            file << line << "\n";
        }
    }
}

void Reports::writeCsv(const std::string& path, const ReportData& data, const DateFilter& filter) const {
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("cannot write " + path);
    }
    file << "report," << csvEscape(data.title) << "\n";
    file << "generated," << csvEscape(nowHuman()) << "\n";
    file << "filter," << csvEscape(filter.label) << "," << filter.from << "," << filter.to << "\n";
    for (const auto& section : data.sections) {
        file << "\nsection," << csvEscape(section.heading) << "\n";
        if (!section.headers.empty()) {
            for (std::size_t i = 0; i < section.headers.size(); ++i) {
                if (i > 0) file << ",";
                file << csvEscape(section.headers[i]);
            }
            file << "\n";
            for (const auto& row : section.rows) {
                for (std::size_t i = 0; i < section.headers.size(); ++i) {
                    if (i > 0) file << ",";
                    file << csvEscape(i < row.size() ? row[i] : "");
                }
                file << "\n";
            }
        }
        for (const auto& line : section.lines) {
            file << "line," << csvEscape(line) << "\n";
        }
    }
}

void Reports::offerExport(const Session& session, const ReportData& preview,
                          const std::function<ReportData(const DateFilter&)>& builder) {
    ui_.printHeader("Preview before export");
    printReport(preview);
    if (!ui_.confirm("Export this report?")) {
        return;
    }
    ui_.printHeader("Export");
    const DateFilter filter = askDateFilter(true);
    const int format = ui_.pick("Format: ", {"TXT", "CSV", "Both"});
    if (format == 0) {
        return;
    }
    std::filesystem::create_directories("reports");
    const std::string stamp = nowStamp();
    try {
        const ReportData data = builder(filter);
        std::vector<std::string> written;
        if (format == 1 || format == 3) {
            const std::string path = "reports/" + data.slug + "_" + stamp + ".txt";
            writeTxt(path, data, filter);
            written.push_back(path);
        }
        if (format == 2 || format == 3) {
            const std::string path = "reports/" + data.slug + "_" + stamp + ".csv";
            writeCsv(path, data, filter);
            written.push_back(path);
        }
        for (const auto& path : written) {
            audit_.write(session.userId, "REPORT_EXPORT", path, true);
            ui_.printSuccess("Wrote " + path);
        }
    } catch (const std::exception& ex) {
        fail(ui_, ex);
        audit_.write(session.userId, "REPORT_EXPORT", preview.slug, false);
    }
}

Reports::ReportData Reports::buildCaseStatus(const DateFilter& filter) {
    ReportData data;
    data.title = "Case status summary";
    data.slug = "case_status";
    data.notes.push_back("Opened vs closed counts by month. Filter: " + filter.label + ".");
    Section section;
    section.heading = "Opened vs closed per month";
    section.headers = {"Month", "Opened", "Closed"};
    auto stmt = db_.prepare(
        "SELECT month, "
        "SUM(opened_n) AS opened_n, "
        "SUM(closed_n) AS closed_n "
        "FROM ("
        "  SELECT DATE_FORMAT(opened_date, '%Y-%m') AS month, COUNT(*) AS opened_n, 0 AS closed_n "
        "  FROM cases WHERE is_active = 1 AND opened_date BETWEEN ? AND ? "
        "  GROUP BY DATE_FORMAT(opened_date, '%Y-%m') "
        "  UNION ALL "
        "  SELECT DATE_FORMAT(closed_date, '%Y-%m') AS month, 0 AS opened_n, COUNT(*) AS closed_n "
        "  FROM cases WHERE is_active = 1 AND status = 'Closed' "
        "    AND closed_date IS NOT NULL AND closed_date BETWEEN ? AND ? "
        "  GROUP BY DATE_FORMAT(closed_date, '%Y-%m')"
        ") AS months "
        "GROUP BY month ORDER BY month");
    stmt->setString(1, filter.from);
    stmt->setString(2, filter.to);
    stmt->setString(3, filter.from);
    stmt->setString(4, filter.to);
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    while (rs->next()) {
        section.rows.push_back({
            rs->getString("month"),
            cellOf(*rs, "opened_n"),
            cellOf(*rs, "closed_n")
        });
    }
    data.sections.push_back(section);
    return data;
}

Reports::ReportData Reports::buildSummaryLists(const DateFilter& filter) {
    ReportData data;
    data.title = "Summary lists";
    data.slug = "summary_lists";
    data.notes.push_back("Top 5 busiest investigators, overdue cases, unverified evidence. Filter: " + filter.label + ".");

    Section busiest;
    busiest.heading = "Top 5 busiest investigators";
    busiest.headers = {"ID", "Name", "Open cases"};
    {
        auto stmt = db_.prepare(
            "SELECT i.investigator_id, i.name, COUNT(c.case_id) AS open_cases "
            "FROM investigators i "
            "JOIN cases c ON c.assigned_investigator_id = i.investigator_id "
            "WHERE c.is_active = 1 AND c.status IN ('Open', 'In Progress') "
            "AND c.opened_date BETWEEN ? AND ? "
            "GROUP BY i.investigator_id, i.name "
            "ORDER BY open_cases DESC, i.name "
            "LIMIT 5");
        stmt->setString(1, filter.from);
        stmt->setString(2, filter.to);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        while (rs->next()) {
            busiest.rows.push_back({
                cellOf(*rs, "investigator_id"),
                rs->getString("name"),
                cellOf(*rs, "open_cases")
            });
        }
    }
    data.sections.push_back(busiest);

    Section overdue;
    overdue.heading = "Overdue cases (deadline before today, not closed)";
    overdue.headers = {"ID", "Title", "Priority", "Deadline", "Days overdue", "Investigator"};
    {
        auto stmt = db_.prepare(
            "SELECT c.case_id, c.title, c.priority, c.deadline, "
            "DATEDIFF(CURDATE(), c.deadline) AS days_overdue, i.name AS investigator "
            "FROM cases c "
            "JOIN investigators i ON c.assigned_investigator_id = i.investigator_id "
            "WHERE c.is_active = 1 AND c.status <> 'Closed' AND c.deadline < CURDATE() "
            "AND c.deadline BETWEEN ? AND ? "
            "ORDER BY c.deadline");
        stmt->setString(1, filter.from);
        stmt->setString(2, filter.to);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        while (rs->next()) {
            overdue.rows.push_back({
                cellOf(*rs, "case_id"),
                rs->getString("title"),
                rs->getString("priority"),
                cellOf(*rs, "deadline"),
                cellOf(*rs, "days_overdue"),
                rs->getString("investigator")
            });
        }
    }
    data.sections.push_back(overdue);

    Section unverified;
    unverified.heading = "Unverified evidence";
    unverified.headers = {"ID", "Case", "Type", "Collected", "Location"};
    {
        auto stmt = db_.prepare(
            "SELECT evidence_id, case_id, type, collected_date, storage_location "
            "FROM evidence "
            "WHERE is_active = 1 AND verified = 0 "
            "AND collected_date BETWEEN ? AND ? "
            "ORDER BY evidence_id");
        stmt->setString(1, filter.from);
        stmt->setString(2, filter.to);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        while (rs->next()) {
            unverified.rows.push_back({
                cellOf(*rs, "evidence_id"),
                cellOf(*rs, "case_id"),
                rs->getString("type"),
                cellOf(*rs, "collected_date"),
                rs->getString("storage_location")
            });
        }
    }
    data.sections.push_back(unverified);
    return data;
}

Reports::ReportData Reports::buildCustody(const DateFilter& filter, int evidenceId, bool mask) {
    ReportData data;
    data.title = "Chain of custody timeline";
    data.slug = "custody_timeline";
    data.notes.push_back("Transfers for evidence ID " + std::to_string(evidenceId) +
                         ". Filter: " + filter.label + ".");
    Section section;
    section.heading = "Timeline";
    section.headers = {"When", "From", "To", "Reason", "Description", "Location"};
    auto stmt = db_.prepare(
        "SELECT t.transfer_datetime, t.from_person, t.to_person, t.reason, "
        "e.description, e.storage_location "
        "FROM custody_transfers t "
        "JOIN evidence e ON e.evidence_id = t.evidence_id "
        "WHERE t.evidence_id = ? AND DATE(t.transfer_datetime) BETWEEN ? AND ? "
        "ORDER BY t.transfer_datetime");
    stmt->setInt(1, evidenceId);
    stmt->setString(2, filter.from);
    stmt->setString(3, filter.to);
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    while (rs->next()) {
        section.rows.push_back({
            cellOf(*rs, "transfer_datetime"),
            rs->getString("from_person"),
            rs->getString("to_person"),
            rs->getString("reason"),
            mask ? "********" : rs->getString("description"),
            mask ? "********" : rs->getString("storage_location")
        });
    }
    data.sections.push_back(section);
    return data;
}

Reports::ReportData Reports::buildTypeChart(const DateFilter& filter) {
    ReportData data;
    data.title = "Cases per type";
    data.slug = "cases_per_type";
    data.notes.push_back("Bar length is scaled to the largest count. Filter: " + filter.label + ".");
    Section section;
    section.heading = "Bar chart";
    auto stmt = db_.prepare(
        "SELECT type, COUNT(*) AS n "
        "FROM cases WHERE is_active = 1 AND opened_date BETWEEN ? AND ? "
        "GROUP BY type ORDER BY n DESC, type");
    stmt->setString(1, filter.from);
    stmt->setString(2, filter.to);
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    std::vector<std::pair<std::string, int>> items;
    int maxValue = 0;
    while (rs->next()) {
        const int n = rs->getInt("n");
        items.push_back({rs->getString("type"), n});
        if (n > maxValue) {
            maxValue = n;
        }
    }
    if (items.empty()) {
        section.lines.push_back("No rows.");
    } else {
        for (const auto& item : items) {
            section.lines.push_back(
                padRight(item.first, 16) + " |" + bar(item.second, maxValue, 30) +
                "| " + std::to_string(item.second));
        }
    }
    data.sections.push_back(section);
    return data;
}

Reports::ReportData Reports::buildMonthlyTrend(const DateFilter& filter) {
    ReportData data;
    data.title = "Monthly trend and priority share";
    data.slug = "monthly_trend";
    data.notes.push_back("Cases opened per month, then priority share in percent. Filter: " + filter.label + ".");

    Section trend;
    trend.heading = "Cases opened per month";
    {
        auto stmt = db_.prepare(
            "SELECT DATE_FORMAT(opened_date, '%Y-%m') AS month, COUNT(*) AS n "
            "FROM cases WHERE is_active = 1 AND opened_date BETWEEN ? AND ? "
            "GROUP BY DATE_FORMAT(opened_date, '%Y-%m') ORDER BY month");
        stmt->setString(1, filter.from);
        stmt->setString(2, filter.to);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::pair<std::string, int>> items;
        int maxValue = 0;
        while (rs->next()) {
            const int n = rs->getInt("n");
            items.push_back({rs->getString("month"), n});
            if (n > maxValue) {
                maxValue = n;
            }
        }
        if (items.empty()) {
            trend.lines.push_back("No rows.");
        } else {
            for (const auto& item : items) {
                trend.lines.push_back(
                    padRight(item.first, 10) + " |" + bar(item.second, maxValue, 30) +
                    "| " + std::to_string(item.second));
            }
        }
    }
    data.sections.push_back(trend);

    Section share;
    share.heading = "Priority share";
    {
        auto stmt = db_.prepare(
            "SELECT priority, COUNT(*) AS n, "
            "ROUND(COUNT(*) * 100.0 / NULLIF(( "
            "  SELECT COUNT(*) FROM cases "
            "  WHERE is_active = 1 AND opened_date BETWEEN ? AND ?"
            "), 0), 1) AS pct "
            "FROM cases WHERE is_active = 1 AND opened_date BETWEEN ? AND ? "
            "GROUP BY priority "
            "ORDER BY FIELD(priority, 'Critical', 'High', 'Medium', 'Low')");
        stmt->setString(1, filter.from);
        stmt->setString(2, filter.to);
        stmt->setString(3, filter.from);
        stmt->setString(4, filter.to);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        std::vector<std::tuple<std::string, int, double>> items;
        double maxPct = 0.0;
        while (rs->next()) {
            const int n = rs->getInt("n");
            const double pct = std::stod(rs->getString("pct"));
            items.push_back({rs->getString("priority"), n, pct});
            if (pct > maxPct) {
                maxPct = pct;
            }
        }
        if (items.empty()) {
            share.lines.push_back("No rows.");
        } else {
            const int maxBar = maxPct <= 0.0 ? 1 : static_cast<int>(maxPct + 0.5);
            for (const auto& item : items) {
                std::ostringstream pct;
                pct << std::fixed << std::setprecision(1) << std::get<2>(item);
                share.lines.push_back(
                    padRight(std::get<0>(item), 10) + " |" +
                    bar(static_cast<int>(std::get<2>(item) + 0.5), maxBar, 30) +
                    "| " + pct.str() + "% (" + std::to_string(std::get<1>(item)) + ")");
            }
        }
    }
    data.sections.push_back(share);
    return data;
}

Reports::ReportData Reports::buildAudit(const DateFilter& filter) {
    ReportData data;
    data.title = "Audit log report";
    data.slug = "audit_log";
    data.notes.push_back("Admin only. Filter: " + filter.label + ".");

    Section entries;
    entries.heading = "Audit entries";
    entries.headers = {"Time", "User", "Action", "Target", "Result"};
    {
        auto stmt = db_.prepare(
            "SELECT a.timestamp, IFNULL(u.username, '-') AS username, "
            "a.action, a.target, a.result "
            "FROM audit_log a "
            "LEFT JOIN users u ON u.user_id = a.user_id "
            "WHERE DATE(a.timestamp) BETWEEN ? AND ? "
            "ORDER BY a.timestamp");
        stmt->setString(1, filter.from);
        stmt->setString(2, filter.to);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        while (rs->next()) {
            entries.rows.push_back({
                cellOf(*rs, "timestamp"),
                rs->getString("username"),
                rs->getString("action"),
                rs->getString("target"),
                rs->getString("result")
            });
        }
    }
    data.sections.push_back(entries);

    Section fails;
    fails.heading = "Failed logins per day";
    fails.headers = {"Day", "Failed logins"};
    {
        auto stmt = db_.prepare(
            "SELECT DATE(timestamp) AS day, COUNT(*) AS n "
            "FROM audit_log "
            "WHERE action = 'LOGIN' AND result = 'FAIL' "
            "AND DATE(timestamp) BETWEEN ? AND ? "
            "GROUP BY DATE(timestamp) ORDER BY day");
        stmt->setString(1, filter.from);
        stmt->setString(2, filter.to);
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        while (rs->next()) {
            fails.rows.push_back({cellOf(*rs, "day"), cellOf(*rs, "n")});
        }
    }
    data.sections.push_back(fails);
    return data;
}

void Reports::caseStatus(const Session& session) {
    try {
        const DateFilter filter = askDateFilter(true);
        const ReportData data = buildCaseStatus(filter);
        printReport(data);
        offerExport(session, data, [this](const DateFilter& f) { return buildCaseStatus(f); });
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Reports::summaryLists(const Session& session) {
    try {
        const DateFilter filter = askDateFilter(true);
        const ReportData data = buildSummaryLists(filter);
        printReport(data);
        offerExport(session, data, [this](const DateFilter& f) { return buildSummaryLists(f); });
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Reports::custodyTimeline(const Session& session) {
    const int evidenceId = ui_.readInt("Evidence ID: ", 1, 1000000);
    try {
        const DateFilter filter = askDateFilter(true);
        const bool mask = (session.role == Role::Viewer);
        const ReportData data = buildCustody(filter, evidenceId, mask);
        printReport(data);
        offerExport(session, data, [this, evidenceId, mask](const DateFilter& f) {
            return buildCustody(f, evidenceId, mask);
        });
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Reports::typeChart(const Session& session) {
    try {
        const DateFilter filter = askDateFilter(true);
        const ReportData data = buildTypeChart(filter);
        printReport(data);
        offerExport(session, data, [this](const DateFilter& f) { return buildTypeChart(f); });
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Reports::monthlyTrend(const Session& session) {
    try {
        const DateFilter filter = askDateFilter(true);
        const ReportData data = buildMonthlyTrend(filter);
        printReport(data);
        offerExport(session, data, [this](const DateFilter& f) { return buildMonthlyTrend(f); });
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}

void Reports::auditReport(const Session& session) {
    if (session.role != Role::Admin) {
        ui_.printError("Access denied.");
        ui_.pause();
        return;
    }
    try {
        const DateFilter filter = askDateFilter(true);
        const ReportData data = buildAudit(filter);
        printReport(data);
        offerExport(session, data, [this](const DateFilter& f) { return buildAudit(f); });
    } catch (const std::exception& ex) {
        fail(ui_, ex);
    }
    ui_.pause();
}
