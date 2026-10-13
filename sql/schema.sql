-- SecureTrace schema (MySQL 8). 3NF: independent investigators/users,
-- cases reference investigators, evidence references cases,
-- custody references evidence, audit_log optionally references users.
CREATE DATABASE IF NOT EXISTS securetrace
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_unicode_ci;
USE securetrace;

CREATE TABLE investigators (
    investigator_id INT AUTO_INCREMENT PRIMARY KEY,
    name            VARCHAR(100) NOT NULL,
    role            VARCHAR(80)  NOT NULL,
    specialty       VARCHAR(80)  NOT NULL,
    department      VARCHAR(80)  NOT NULL,
    CONSTRAINT chk_inv_name CHECK (CHAR_LENGTH(name) BETWEEN 2 AND 100)
) ENGINE=InnoDB;

CREATE TABLE users (
    user_id          INT AUTO_INCREMENT PRIMARY KEY,
    username         VARCHAR(32)  NOT NULL,
    password_hash    CHAR(64)     NOT NULL,
    salt             CHAR(32)     NOT NULL,
    role             VARCHAR(20)  NOT NULL,
    failed_attempts  INT          NOT NULL DEFAULT 0,
    locked_until     DATETIME     NULL,
    is_active        TINYINT(1)   NOT NULL DEFAULT 1,
    CONSTRAINT uq_users_username UNIQUE (username),
    CONSTRAINT chk_users_username CHECK (username REGEXP '^[A-Za-z0-9_]{3,32}$'),
    CONSTRAINT chk_users_role CHECK (role IN ('admin', 'investigator', 'viewer')),
    CONSTRAINT chk_users_failed CHECK (failed_attempts >= 0),
    CONSTRAINT chk_users_hash CHECK (password_hash REGEXP '^[0-9a-f]{64}$'),
    CONSTRAINT chk_users_salt CHECK (salt REGEXP '^[0-9a-f]{32}$'),
    CONSTRAINT chk_users_active CHECK (is_active IN (0, 1))
) ENGINE=InnoDB;

CREATE TABLE cases (
    case_id                   INT AUTO_INCREMENT PRIMARY KEY,
    title                     VARCHAR(150) NOT NULL,
    type                      VARCHAR(30)  NOT NULL,
    priority                  VARCHAR(20)  NOT NULL,
    status                    VARCHAR(20)  NOT NULL,
    opened_date               DATE         NOT NULL,
    deadline                  DATE         NOT NULL,
    closed_date               DATE         NULL,
    assigned_investigator_id  INT          NOT NULL,
    is_active                 TINYINT(1)   NOT NULL DEFAULT 1,
    CONSTRAINT fk_cases_investigator
        FOREIGN KEY (assigned_investigator_id)
        REFERENCES investigators(investigator_id)
        ON UPDATE CASCADE
        ON DELETE RESTRICT,
    CONSTRAINT chk_cases_type CHECK (type IN (
        'Fraud', 'Data Breach', 'Insider Threat', 'Malware', 'Other')),
    CONSTRAINT chk_cases_priority CHECK (priority IN (
        'Low', 'Medium', 'High', 'Critical')),
    CONSTRAINT chk_cases_status CHECK (status IN (
        'Open', 'In Progress', 'Closed')),
    CONSTRAINT chk_cases_dates CHECK (deadline >= opened_date),
    CONSTRAINT chk_cases_closed CHECK (
        (status = 'Closed' AND closed_date IS NOT NULL AND closed_date >= opened_date)
        OR (status <> 'Closed' AND closed_date IS NULL)
    ),
    CONSTRAINT chk_cases_active CHECK (is_active IN (0, 1))
) ENGINE=InnoDB;

CREATE TABLE evidence (
    evidence_id       INT AUTO_INCREMENT PRIMARY KEY,
    case_id           INT           NOT NULL,
    description       VARCHAR(255)  NOT NULL,
    type              VARCHAR(30)   NOT NULL,
    sha256_hash       CHAR(64)      NOT NULL,
    size_mb           DECIMAL(10,2) NOT NULL,
    collected_date    DATE          NOT NULL,
    storage_location  VARCHAR(120)  NOT NULL,
    verified          TINYINT(1)    NOT NULL DEFAULT 0,
    is_active         TINYINT(1)    NOT NULL DEFAULT 1,
    CONSTRAINT fk_evidence_case
        FOREIGN KEY (case_id) REFERENCES cases(case_id)
        ON UPDATE CASCADE
        ON DELETE RESTRICT,
    CONSTRAINT chk_evidence_type CHECK (type IN (
        'Laptop', 'USB', 'Phone', 'Disk Image', 'Log File', 'Other')),
    CONSTRAINT chk_evidence_hash CHECK (sha256_hash REGEXP '^[0-9a-f]{64}$'),
    CONSTRAINT chk_evidence_size CHECK (size_mb > 0),
    CONSTRAINT chk_evidence_verified CHECK (verified IN (0, 1)),
    CONSTRAINT chk_evidence_active CHECK (is_active IN (0, 1))
) ENGINE=InnoDB;

-- Append-only chain of custody. Application grants omit UPDATE/DELETE.
CREATE TABLE custody_transfers (
    transfer_id        INT AUTO_INCREMENT PRIMARY KEY,
    evidence_id        INT          NOT NULL,
    from_person        VARCHAR(100) NOT NULL,
    to_person          VARCHAR(100) NOT NULL,
    transfer_datetime  DATETIME     NOT NULL,
    reason             VARCHAR(255) NOT NULL,
    CONSTRAINT fk_custody_evidence
        FOREIGN KEY (evidence_id) REFERENCES evidence(evidence_id)
        ON UPDATE CASCADE
        ON DELETE RESTRICT,
    CONSTRAINT chk_custody_from CHECK (CHAR_LENGTH(from_person) >= 2),
    CONSTRAINT chk_custody_to CHECK (CHAR_LENGTH(to_person) >= 2),
    CONSTRAINT chk_custody_reason CHECK (CHAR_LENGTH(reason) >= 3)
) ENGINE=InnoDB;

CREATE TABLE audit_log (
    audit_id   INT AUTO_INCREMENT PRIMARY KEY,
    user_id    INT          NULL,
    action     VARCHAR(80)  NOT NULL,
    target     VARCHAR(120) NOT NULL,
    timestamp  DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    result     VARCHAR(10)  NOT NULL,
    CONSTRAINT fk_audit_user
        FOREIGN KEY (user_id) REFERENCES users(user_id)
        ON UPDATE CASCADE
        ON DELETE SET NULL,
    CONSTRAINT chk_audit_result CHECK (result IN ('SUCCESS', 'FAIL'))
) ENGINE=InnoDB;

CREATE INDEX idx_cases_status ON cases(status);
CREATE INDEX idx_cases_type ON cases(type);
CREATE INDEX idx_cases_priority ON cases(priority);
CREATE INDEX idx_cases_assigned ON cases(assigned_investigator_id);
CREATE INDEX idx_cases_opened ON cases(opened_date);
CREATE INDEX idx_cases_active ON cases(is_active);
CREATE INDEX idx_evidence_case ON evidence(case_id);
CREATE INDEX idx_evidence_hash ON evidence(sha256_hash);
CREATE INDEX idx_evidence_active ON evidence(is_active);
CREATE INDEX idx_evidence_type ON evidence(type);
CREATE INDEX idx_custody_evidence ON custody_transfers(evidence_id);
CREATE INDEX idx_custody_datetime ON custody_transfers(transfer_datetime);
CREATE INDEX idx_audit_user ON audit_log(user_id);
CREATE INDEX idx_audit_timestamp ON audit_log(timestamp);
CREATE INDEX idx_audit_action ON audit_log(action);

DELIMITER $$

CREATE TRIGGER trg_evidence_soft_delete
AFTER UPDATE ON evidence
FOR EACH ROW
BEGIN
    IF OLD.is_active = 1 AND NEW.is_active = 0 THEN
        INSERT INTO audit_log (user_id, action, target, result)
        VALUES (
            NULLIF(@app_user_id, 0),
            'EVIDENCE_SOFT_DELETE',
            CONCAT('evidence_id=', NEW.evidence_id),
            'SUCCESS'
        );
    END IF;
END$$

CREATE TRIGGER trg_evidence_hard_delete
AFTER DELETE ON evidence
FOR EACH ROW
BEGIN
    INSERT INTO audit_log (user_id, action, target, result)
    VALUES (
        NULLIF(@app_user_id, 0),
        'EVIDENCE_HARD_DELETE',
        CONCAT('evidence_id=', OLD.evidence_id),
        'SUCCESS'
    );
END$$

CREATE TRIGGER trg_case_status_change
AFTER UPDATE ON cases
FOR EACH ROW
BEGIN
    IF OLD.status <> NEW.status THEN
        INSERT INTO audit_log (user_id, action, target, result)
        VALUES (
            NULLIF(@app_user_id, 0),
            'CASE_STATUS_CHANGE',
            CONCAT('case_id=', NEW.case_id, ' ', OLD.status, '->', NEW.status),
            'SUCCESS'
        );
    END IF;
END$$

DELIMITER ;
