-- Least-privilege application account. Run as a MySQL administrator
-- after schema.sql. Change the password to match config/config.ini.
CREATE USER IF NOT EXISTS 'securetrace_app'@'localhost'
    IDENTIFIED BY 'ChangeMe_AppUser_1';

GRANT SELECT, INSERT, UPDATE, DELETE ON securetrace.investigators TO 'securetrace_app'@'localhost';
GRANT SELECT, INSERT, UPDATE, DELETE ON securetrace.cases TO 'securetrace_app'@'localhost';
GRANT SELECT, INSERT, UPDATE, DELETE ON securetrace.evidence TO 'securetrace_app'@'localhost';
GRANT SELECT, INSERT, UPDATE, DELETE ON securetrace.users TO 'securetrace_app'@'localhost';
GRANT SELECT, INSERT ON securetrace.custody_transfers TO 'securetrace_app'@'localhost';
GRANT SELECT, INSERT ON securetrace.audit_log TO 'securetrace_app'@'localhost';

FLUSH PRIVILEGES;
