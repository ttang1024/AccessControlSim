#include "storage/sqlite_event_repository.h"

#include <cstdint>
#include <variant>

#include "core/overloaded.h"
#include "storage/statement.h"
#include "storage/storage_error.h"
#include "storage/transaction.h"
#include "storage/unix_time.h"

namespace acs::storage {

SqliteEventRepository::SqliteEventRepository(Database& db) : m_db(db) {}

namespace {

void insertAccessEvent(Statement& insert, const events::AccessEvent& event) {
    insert.bind(1, toUnixMillis(event.timestamp))
        .bind(2, event.readerId)
        .bind(3, event.card.number)
        .bind(4, static_cast<std::int64_t>(event.card.facilityCode));
    if (event.decision.isGranted()) {
        insert.bind(5, "grant").bindNull(6);
    } else {
        insert.bind(5, "deny").bind(6, core::toString(*event.decision.denyReason()));
    }
    (void)insert.step();
    insert.reset();
}

void insertReaderStatusEvent(Statement& insert, const events::ReaderStatusEvent& event) {
    insert.bind(1, toUnixMillis(event.timestamp))
        .bind(2, event.readerId)
        .bind(3, events::toString(event.status));
    (void)insert.step();
    insert.reset();
}

}  // namespace

bool SqliteEventRepository::append(std::span<const events::Event> events) {
    try {
        Transaction transaction(m_db);
        Statement insertAccess(m_db,
                               "INSERT INTO access_events (timestamp_ms, reader_id, card_number, "
                               "facility, decision, reason) VALUES (?1, ?2, ?3, ?4, ?5, ?6)");
        Statement insertStatus(m_db,
                               "INSERT INTO reader_status_events (timestamp_ms, reader_id, status) "
                               "VALUES (?1, ?2, ?3)");
        for (const events::Event& event : events) {
            std::visit(
                core::Overloaded{
                    [&](const events::AccessEvent& e) { insertAccessEvent(insertAccess, e); },
                    [&](const events::ReaderStatusEvent& e) {
                        insertReaderStatusEvent(insertStatus, e);
                    },
                },
                event);
        }
        transaction.commit();
        return true;
    } catch (const StorageError&) {
        return false;
    }
}

std::vector<events::AccessEvent> SqliteEventRepository::loadRecent(std::size_t limit) const {
    Statement query(m_db,
                    "SELECT timestamp_ms, reader_id, card_number, facility, decision, reason "
                    "FROM access_events ORDER BY id DESC LIMIT ?1");
    query.bind(1, static_cast<std::int64_t>(limit));

    std::vector<events::AccessEvent> result;
    while (query.step()) {
        // An unrecognised reason can only come from a newer build or a manual
        // edit. Read it back as InternalError rather than as a grant.
        const auto decision =
            query.columnText(4) == "grant"
                ? engine::Decision::grant()
                : engine::Decision::deny(core::denyReasonFromString(query.columnText(5))
                                             .value_or(core::DenyReason::InternalError));
        result.push_back({
            .timestamp = fromUnixMillis(query.columnInt(0)),
            .readerId = query.columnText(1),
            .card = {.number = query.columnText(2),
                     .facilityCode = static_cast<std::uint32_t>(query.columnInt(3))},
            .decision = decision,
        });
    }
    return result;
}

std::vector<events::ReaderStatusEvent> SqliteEventRepository::loadRecentReaderStatus(
    std::size_t limit) const {
    Statement query(m_db,
                    "SELECT timestamp_ms, reader_id, status FROM reader_status_events "
                    "ORDER BY id DESC LIMIT ?1");
    query.bind(1, static_cast<std::int64_t>(limit));

    std::vector<events::ReaderStatusEvent> result;
    while (query.step()) {
        result.push_back({
            .timestamp = fromUnixMillis(query.columnInt(0)),
            .readerId = query.columnText(1),
            .status = query.columnText(2) == "online" ? events::ReaderStatus::Online
                                                      : events::ReaderStatus::Offline,
        });
    }
    return result;
}

}  // namespace acs::storage
