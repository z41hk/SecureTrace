-- Run once on a database created in Phase 1.
USE securetrace;
ALTER TABLE cases
    ADD COLUMN is_active TINYINT(1) NOT NULL DEFAULT 1;
ALTER TABLE cases
    ADD CONSTRAINT chk_cases_active CHECK (is_active IN (0, 1));
CREATE INDEX idx_cases_active ON cases(is_active);
