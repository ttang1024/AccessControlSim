#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "core/ids.h"
#include "core/time.h"

namespace acs::core {

// A weekly window covering [start, end), in minutes after midnight on `day`.
//  - end > start:  an ordinary same-day window, e.g. {Monday, 07:00, 18:00}.
//  - end < start:  crosses midnight into the next day, e.g. {Friday, 22:00, 06:00}
//                  covers Friday 22:00 up to Saturday 06:00.
//  - end == start: empty. Use {day, 0h, 24h} for a whole day.
struct TimeWindow {
    std::chrono::weekday day;
    std::chrono::minutes start;
    std::chrono::minutes end;

    [[nodiscard]] bool contains(std::chrono::weekday today, std::chrono::minutes timeOfDay) const;

    friend bool operator==(const TimeWindow&, const TimeWindow&) = default;
};

// A named set of weekly windows, e.g. "Business Hours" = Mon-Fri 07:00-18:00.
struct Schedule {
    ScheduleId id;
    std::string name;
    std::vector<TimeWindow> windows;

    // Times are interpreted as UTC for now (see ADR-003 in docs/decisions.md).
    [[nodiscard]] bool isActiveAt(TimePoint time) const;

    friend bool operator==(const Schedule&, const Schedule&) = default;
};

}  // namespace acs::core
