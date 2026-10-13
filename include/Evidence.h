#pragma once

#include "AuditLog.h"
#include "ConsoleUI.h"
#include "DatabaseConnection.h"
#include "Session.h"

#include <string>
#include <utility>

class Evidence {
public:
    Evidence(DatabaseConnection& db, AuditLog& audit, ConsoleUI& ui);
    void showMenu(const Session& session);

private:
    void create(const Session& session);
    void listAll();
    void readOne();
    void update(const Session& session);
    void remove(const Session& session);
    void search();
    void restore(const Session& session);
    void verify(const Session& session);

    bool caseIsActive(int caseId);
    std::pair<std::string, double> hashFromUser();
    bool deny(const Session& session);

    DatabaseConnection& db_;
    AuditLog& audit_;
    ConsoleUI& ui_;
};
