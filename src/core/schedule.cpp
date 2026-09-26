#include "core/schedule.h"

#include <algorithm>

namespace acs::core {

bool TimeWindow::contains(std::chrono::weekday today, std::chrono::minutes timeOfDay) const {
    if (start < end) {
        return today == day && timeOfDay >= start && timeOfDay < end;
    }
    if (end < start) {
        // weekday arithmetic wraps, so Saturday + 1 day is Sunday.
        const bool lateOnStartDay = today == day && timeOfDay >= start;
        const bool earlyOnNextDay = today == day + std::chrono::days{1} && timeOfDay < end;
        return lateOnStartDay || earlyOnNextDay;
    }
    return false;
}

bool Schedule::isActiveAt(TimePoint time) const {
    const auto midnight = std::chrono::floor<std::chrono::days>(time);
    const std::chrono::weekday today{midnight};
    const auto timeOfDay = std::chrono::floor<std::chrono::minutes>(time - midnight);

    return std::ranges::any_of(
        windows, [&](const TimeWindow& window) { return window.contains(today, timeOfDay); });
}

}  // namespace acs::core
