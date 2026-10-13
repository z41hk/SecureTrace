#pragma once

#include "AuditLog.h"
#include "ConsoleUI.h"
#include "DatabaseConnection.h"
#include "Session.h"

#include <functional>
#include <string>
#include <vector>

class Reports {
public:
    Reports(DatabaseConnection& db, AuditLog& audit, ConsoleUI& ui);
    void showMenu(const Session& session);
    void showSummary(const Session& session);
    void auditReport(const Session& session);

private:
    struct DateFilter {
        std::string from;
        std::string to;
        std::string label;
    };

    struct Section {
        std::string heading;
        std::vector<std::string> headers;
        std::vector<std::vector<std::string>> rows;
        std::vector<std::string> lines;
    };

    struct ReportData {
        std::string title;
        std::string slug;
        std::vector<std::string> notes;
        std::vector<Section> sections;
    };

    void caseStatus(const Session& session);
    void summaryLists(const Session& session);
    void custodyTimeline(const Session& session);
    void typeChart(const Session& session);
    void monthlyTrend(const Session& session);

    ReportData buildCaseStatus(const DateFilter& filter);
    ReportData buildSummaryLists(const DateFilter& filter);
    ReportData buildCustody(const DateFilter& filter, int evidenceId, bool mask);
    ReportData buildTypeChart(const DateFilter& filter);
    ReportData buildMonthlyTrend(const DateFilter& filter);
    ReportData buildAudit(const DateFilter& filter);

    void printReport(const ReportData& data) const;
    void offerExport(const Session& session, const ReportData& preview,
                     const std::function<ReportData(const DateFilter&)>& builder);
    void writeTxt(const std::string& path, const ReportData& data, const DateFilter& filter) const;
    void writeCsv(const std::string& path, const ReportData& data, const DateFilter& filter) const;

    DateFilter allDates() const;
    DateFilter askDateFilter(bool allowAll);
    std::string bar(int value, int maxValue, int width) const;

    DatabaseConnection& db_;
    AuditLog& audit_;
    ConsoleUI& ui_;
};
