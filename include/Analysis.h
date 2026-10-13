#pragma once

#include "ConsoleUI.h"
#include "DatabaseConnection.h"
#include "Session.h"

class Analysis {
public:
    Analysis(DatabaseConnection& db, ConsoleUI& ui);
    void showMenu(const Session& session);

private:
    void unaryMenu();
    void unaryCount();
    void unaryMax();
    void unaryMin();

    void groupingMenu();
    void groupingCases();
    void groupingEvidenceSize();
    void groupingAvgDuration();
    void groupingBusyInvestigators();

    void joinMenu(const Session& session);
    void fullCaseFile(const Session& session);
    void custodyHistory(const Session& session);

    void subqueryMenu(const Session& session);
    void subqueryNoEvidence();
    void subqueryAboveAverage();
    void subqueryNeverTransferred(const Session& session);
    void subqueryEarlyDeadline();

    void decisionMenu();
    void decisionWorkload();
    void decisionIntegrity();
    void decisionDaysRemaining();

    bool deny(const Session& session);

    DatabaseConnection& db_;
    ConsoleUI& ui_;
};
