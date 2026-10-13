#pragma once

#include "AuditLog.h"
#include "ConsoleUI.h"
#include "DatabaseConnection.h"
#include "Session.h"

class CustodyTransfers {
public:
    CustodyTransfers(DatabaseConnection& db, AuditLog& audit, ConsoleUI& ui);
    void showMenu(const Session& session);

private:
    void create(const Session& session);
    void listAll();
    void readOne();
    void search();
    bool evidenceIsActive(int evidenceId);
    bool deny(const Session& session);

    DatabaseConnection& db_;
    AuditLog& audit_;
    ConsoleUI& ui_;
};
