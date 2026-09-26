#pragma once

#include <set>
#include <string>

#include "core/ids.h"

namespace acs::core {

// Grants entry to a set of zones, but only while its schedule is active.
struct AccessGroup {
    AccessGroupId id;
    std::string name;
    std::set<ZoneId> zones;
    ScheduleId scheduleId;

    friend bool operator==(const AccessGroup&, const AccessGroup&) = default;
};

}  // namespace acs::core
