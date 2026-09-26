#pragma once

#include <map>
#include <vector>

#include "core/access_group.h"
#include "core/card.h"
#include "core/cardholder.h"
#include "core/ids.h"
#include "core/reader.h"
#include "core/schedule.h"
#include "core/zone.h"

namespace acs::engine {

// All site data, held by value in plain maps. Built by the storage layer
// (IAccessDataRepository::loadSnapshot). Callers treat a snapshot as immutable
// once built, so many threads can read it without locks (see ADR-001).
struct AccessSnapshot {
    // The engine doesn't read zones. They are here so the snapshot holds the
    // whole site and can be saved to the database and loaded back unchanged.
    std::map<core::ZoneId, core::Zone> zones;
    std::map<core::ReaderId, core::Reader> readers;
    std::map<core::CardCredential, core::Card> cards;
    std::map<core::CardholderId, core::Cardholder> cardholders;
    std::map<core::AccessGroupId, core::AccessGroup> accessGroups;
    std::map<core::ScheduleId, core::Schedule> schedules;

    // Every configured reader, e.g. to seed the heartbeat monitor.
    [[nodiscard]] std::vector<core::ReaderId> readerIds() const {
        std::vector<core::ReaderId> ids;
        ids.reserve(readers.size());
        for (const auto& [id, reader] : readers) {
            ids.push_back(id);
        }
        return ids;
    }

    friend bool operator==(const AccessSnapshot&, const AccessSnapshot&) = default;
};

}  // namespace acs::engine
