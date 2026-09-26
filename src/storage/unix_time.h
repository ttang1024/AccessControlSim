#pragma once

#include <chrono>
#include <cstdint>

#include "core/time.h"

namespace acs::storage {

// Timestamps are stored as integer milliseconds since the Unix epoch (UTC).
// Integers sort and compare correctly in SQL and need no parsing.
[[nodiscard]] inline std::int64_t toUnixMillis(core::TimePoint time) {
    return std::chrono::floor<std::chrono::milliseconds>(time).time_since_epoch().count();
}

[[nodiscard]] inline core::TimePoint fromUnixMillis(std::int64_t millis) {
    return core::TimePoint{std::chrono::milliseconds{millis}};
}

}  // namespace acs::storage
