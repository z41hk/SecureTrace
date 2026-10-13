#pragma once

#include "AuditLog.h"
#include "ConsoleUI.h"
#include "DatabaseConnection.h"
#include "Session.h"

class Cases {
public:
    Cases(DatabaseConnection& db, AuditLog& audit, ConsoleUI& ui);
    void showMenu(const Session& session);

private:
    void create(const Session& session);
    void listAll();
    void readOne();
    void update(const Session& session);
    void remove(const Session& session);
    void search();
    void restore(const Session& session);

    bool investigatorExists(int investigatorId);
    int unverifiedCount(int caseId);
    bool deny(const Session& session);

    DatabaseConnection& db_;
    AuditLog& audit_;
    ConsoleUI& ui_;
};
