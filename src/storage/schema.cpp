#include "storage/schema.h"

namespace acs::storage {

namespace {

// Site data uses foreign keys, so a broken reference can't be stored (see
// ADR-002). access_events deliberately has none: the audit log must record
// swipes from unknown readers and cards, and must outlive later deletions.
// The same applies to reader_status_events.
constexpr const char* kSchema = R"sql(
CREATE TABLE IF NOT EXISTS zones (
    id   TEXT PRIMARY KEY,
    name TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS readers (
    id      TEXT PRIMARY KEY,
    zone_id TEXT NOT NULL REFERENCES zones(id)
);

CREATE TABLE IF NOT EXISTS schedules (
    id   TEXT PRIMARY KEY,
    name TEXT NOT NULL
);

-- day: 0 = Sunday ... 6 = Saturday. Minutes since midnight; end < start
-- means the window crosses midnight (ADR-003).
CREATE TABLE IF NOT EXISTS schedule_windows (
    schedule_id  TEXT    NOT NULL REFERENCES schedules(id),
    day          INTEGER NOT NULL CHECK (day BETWEEN 0 AND 6),
    start_minute INTEGER NOT NULL CHECK (start_minute BETWEEN 0 AND 1439),
    end_minute   INTEGER NOT NULL CHECK (end_minute BETWEEN 0 AND 1440)
);

CREATE TABLE IF NOT EXISTS access_groups (
    id          TEXT PRIMARY KEY,
    name        TEXT NOT NULL,
    schedule_id TEXT NOT NULL REFERENCES schedules(id)
);

CREATE TABLE IF NOT EXISTS access_group_zones (
    group_id TEXT NOT NULL REFERENCES access_groups(id),
    zone_id  TEXT NOT NULL REFERENCES zones(id),
    PRIMARY KEY (group_id, zone_id)
);

CREATE TABLE IF NOT EXISTS cardholders (
    id         TEXT PRIMARY KEY,
    name       TEXT NOT NULL,
    status     TEXT NOT NULL CHECK (status IN ('active', 'suspended', 'expired')),
    expires_at INTEGER  -- Unix ms; NULL = never expires
);

CREATE TABLE IF NOT EXISTS cardholder_groups (
    cardholder_id TEXT NOT NULL REFERENCES cardholders(id),
    group_id      TEXT NOT NULL REFERENCES access_groups(id),
    PRIMARY KEY (cardholder_id, group_id)
);

CREATE TABLE IF NOT EXISTS cards (
    number        TEXT    NOT NULL,
    facility      INTEGER NOT NULL,
    cardholder_id TEXT    NOT NULL REFERENCES cardholders(id),
    PRIMARY KEY (number, facility)
);

CREATE TABLE IF NOT EXISTS access_events (
    id           INTEGER PRIMARY KEY,
    timestamp_ms INTEGER NOT NULL,
    reader_id    TEXT    NOT NULL,
    card_number  TEXT    NOT NULL,
    facility     INTEGER NOT NULL,
    decision     TEXT    NOT NULL CHECK (decision IN ('grant', 'deny')),
    reason       TEXT    -- NULL for grants
);

CREATE INDEX IF NOT EXISTS access_events_by_time ON access_events(timestamp_ms);

CREATE TABLE IF NOT EXISTS reader_status_events (
    id           INTEGER PRIMARY KEY,
    timestamp_ms INTEGER NOT NULL,
    reader_id    TEXT    NOT NULL,
    status       TEXT    NOT NULL CHECK (status IN ('online', 'offline'))
);
)sql";

}  // namespace

void applySchema(Database& db) { db.execute(kSchema); }

}  // namespace acs::storage
