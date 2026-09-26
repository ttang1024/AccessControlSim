#pragma once

#include <chrono>

#include "core/time.h"

namespace acs::test {

using namespace std::chrono_literals;

// A fixed reference week, so tests never depend on the real clock.
inline constexpr std::chrono::year_month_day kMonday{std::chrono::year{2026},
                                                     std::chrono::September, std::chrono::day{21}};
static_assert(std::chrono::weekday{std::chrono::sys_days{kMonday}} == std::chrono::Monday);

// The time on the given day of the reference week, e.g. at(Friday, 22h, 30min).
inline core::TimePoint at(std::chrono::weekday day, std::chrono::hours hour,
                          std::chrono::minutes minute = 0min, std::chrono::seconds second = 0s) {
    const auto offset = std::chrono::days{(day - std::chrono::Monday).count()};
    return std::chrono::sys_days{kMonday} + offset + hour + minute + second;
}

}  // namespace acs::test
