#include "storage/sqlite_access_data_repository.h"

#include <chrono>
#include <cstdint>

#include "storage/statement.h"
#include "storage/storage_error.h"
#include "storage/transaction.h"
#include "storage/unix_time.h"

namespace acs::storage {

namespace {

// Runs one INSERT and readies the statement for reuse. SQLite's error for a
// broken reference is just "FOREIGN KEY constraint failed", so we add which
// row it was, e.g. "reader 'R-1': FOREIGN KEY constraint failed".
void insertRow(Statement& insert, const std::string& what) {
    try {
        (void)insert.step();
    } catch (const StorageError& e) {
        throw StorageError(what + ": " + e.what());
    }
    insert.reset();
}

}  // namespace

SqliteAccessDataRepository::SqliteAccessDataRepository(Database& db) : m_db(db) {}

// Child rows are read ORDER BY rowid, which is insertion order, so vectors
// come back in the order they were saved.
engine::AccessSnapshot SqliteAccessDataRepository::loadSnapshot() const {
    engine::AccessSnapshot snapshot;

    Statement zones(m_db, "SELECT id, name FROM zones");
    while (zones.step()) {
        core::Zone zone{.id = zones.columnText(0), .name = zones.columnText(1)};
        snapshot.zones.emplace(zone.id, std::move(zone));
    }

    Statement readers(m_db, "SELECT id, zone_id FROM readers");
    while (readers.step()) {
        core::Reader reader{.id = readers.columnText(0), .zoneId = readers.columnText(1)};
        snapshot.readers.emplace(reader.id, std::move(reader));
    }

    Statement schedules(m_db, "SELECT id, name FROM schedules");
    while (schedules.step()) {
        core::Schedule schedule{
            .id = schedules.columnText(0), .name = schedules.columnText(1), .windows = {}};
        snapshot.schedules.emplace(schedule.id, std::move(schedule));
    }
    Statement windows(m_db,
                      "SELECT schedule_id, day, start_minute, end_minute FROM schedule_windows "
                      "ORDER BY rowid");
    while (windows.step()) {
        snapshot.schedules.at(windows.columnText(0))
            .windows.push_back({
                .day = std::chrono::weekday{static_cast<unsigned>(windows.columnInt(1))},
                .start = std::chrono::minutes{windows.columnInt(2)},
                .end = std::chrono::minutes{windows.columnInt(3)},
            });
    }

    Statement groups(m_db, "SELECT id, name, schedule_id FROM access_groups");
    while (groups.step()) {
        core::AccessGroup group{.id = groups.columnText(0),
                                .name = groups.columnText(1),
                                .zones = {},
                                .scheduleId = groups.columnText(2)};
        snapshot.accessGroups.emplace(group.id, std::move(group));
    }
    Statement groupZones(m_db, "SELECT group_id, zone_id FROM access_group_zones");
    while (groupZones.step()) {
        snapshot.accessGroups.at(groupZones.columnText(0)).zones.insert(groupZones.columnText(1));
    }

    Statement cardholders(m_db, "SELECT id, name, status, expires_at FROM cardholders");
    while (cardholders.step()) {
        const auto status = core::cardholderStatusFromString(cardholders.columnText(2));
        if (!status) {
            throw StorageError("invalid status for cardholder " + cardholders.columnText(0));
        }
        core::Cardholder cardholder{
            .id = cardholders.columnText(0),
            .name = cardholders.columnText(1),
            .status = *status,
            .expiresAt = cardholders.columnIsNull(3)
                             ? std::nullopt
                             : std::optional{fromUnixMillis(cardholders.columnInt(3))},
            .accessGroups = {},
        };
        snapshot.cardholders.emplace(cardholder.id, std::move(cardholder));
    }
    Statement memberships(m_db,
                          "SELECT cardholder_id, group_id FROM cardholder_groups ORDER BY rowid");
    while (memberships.step()) {
        snapshot.cardholders.at(memberships.columnText(0))
            .accessGroups.push_back(memberships.columnText(1));
    }

    Statement cards(m_db, "SELECT number, facility, cardholder_id FROM cards");
    while (cards.step()) {
        const core::CardCredential credential{
            .number = cards.columnText(0),
            .facilityCode = static_cast<std::uint32_t>(cards.columnInt(1))};
        snapshot.cards.emplace(
            credential, core::Card{.credential = credential, .cardholderId = cards.columnText(2)});
    }

    return snapshot;
}

void SqliteAccessDataRepository::replaceAll(const engine::AccessSnapshot& snapshot) {
    Transaction transaction(m_db);

    // Children first, so no foreign key is ever left pointing at a deleted row.
    m_db.execute(
        "DELETE FROM cards; DELETE FROM cardholder_groups; DELETE FROM cardholders;"
        "DELETE FROM access_group_zones; DELETE FROM access_groups;"
        "DELETE FROM schedule_windows; DELETE FROM schedules;"
        "DELETE FROM readers; DELETE FROM zones;");

    // Parents first, so each foreign key finds its target already stored. A
    // broken reference makes SQLite throw here, and the Transaction destructor
    // then rolls everything back, including the deletes above.
    Statement insertZone(m_db, "INSERT INTO zones (id, name) VALUES (?1, ?2)");
    for (const auto& [id, zone] : snapshot.zones) {
        insertZone.bind(1, id).bind(2, zone.name);
        insertRow(insertZone, "zone '" + id + "'");
    }

    Statement insertSchedule(m_db, "INSERT INTO schedules (id, name) VALUES (?1, ?2)");
    Statement insertWindow(m_db,
                           "INSERT INTO schedule_windows (schedule_id, day, start_minute, "
                           "end_minute) VALUES (?1, ?2, ?3, ?4)");
    for (const auto& [id, schedule] : snapshot.schedules) {
        insertSchedule.bind(1, id).bind(2, schedule.name);
        insertRow(insertSchedule, "schedule '" + id + "'");
        for (const core::TimeWindow& window : schedule.windows) {
            insertWindow.bind(1, id)
                .bind(2, static_cast<std::int64_t>(window.day.c_encoding()))
                .bind(3, static_cast<std::int64_t>(window.start.count()))
                .bind(4, static_cast<std::int64_t>(window.end.count()));
            insertRow(insertWindow, "schedule '" + id + "' window");
        }
    }

    Statement insertReader(m_db, "INSERT INTO readers (id, zone_id) VALUES (?1, ?2)");
    for (const auto& [id, reader] : snapshot.readers) {
        insertReader.bind(1, id).bind(2, reader.zoneId);
        insertRow(insertReader, "reader '" + id + "'");
    }

    Statement insertGroup(m_db,
                          "INSERT INTO access_groups (id, name, schedule_id) VALUES (?1, ?2, ?3)");
    Statement insertGroupZone(m_db,
                              "INSERT INTO access_group_zones (group_id, zone_id) VALUES (?1, ?2)");
    for (const auto& [id, group] : snapshot.accessGroups) {
        insertGroup.bind(1, id).bind(2, group.name).bind(3, group.scheduleId);
        insertRow(insertGroup, "access group '" + id + "'");
        for (const core::ZoneId& zoneId : group.zones) {
            insertGroupZone.bind(1, id).bind(2, zoneId);
            insertRow(insertGroupZone, "access group '" + id + "' zone '" + zoneId + "'");
        }
    }

    Statement insertCardholder(
        m_db, "INSERT INTO cardholders (id, name, status, expires_at) VALUES (?1, ?2, ?3, ?4)");
    Statement insertMembership(
        m_db, "INSERT INTO cardholder_groups (cardholder_id, group_id) VALUES (?1, ?2)");
    for (const auto& [id, cardholder] : snapshot.cardholders) {
        insertCardholder.bind(1, id).bind(2, cardholder.name).bind(3, toString(cardholder.status));
        if (cardholder.expiresAt) {
            insertCardholder.bind(4, toUnixMillis(*cardholder.expiresAt));
        } else {
            insertCardholder.bindNull(4);
        }
        insertRow(insertCardholder, "cardholder '" + id + "'");
        for (const core::AccessGroupId& groupId : cardholder.accessGroups) {
            insertMembership.bind(1, id).bind(2, groupId);
            insertRow(insertMembership, "cardholder '" + id + "' group '" + groupId + "'");
        }
    }

    Statement insertCard(m_db,
                         "INSERT INTO cards (number, facility, cardholder_id) VALUES (?1, ?2, ?3)");
    for (const auto& [credential, card] : snapshot.cards) {
        insertCard.bind(1, credential.number)
            .bind(2, static_cast<std::int64_t>(credential.facilityCode))
            .bind(3, card.cardholderId);
        insertRow(insertCard, "card '" + credential.number + "'");
    }

    transaction.commit();
}

}  // namespace acs::storage
