#pragma once

#include <string_view>

#include "core/ids.h"
#include "core/time.h"

namespace acs::events {

enum class ReaderStatus { Online, Offline };

[[nodiscard]] constexpr std::string_view toString(ReaderStatus status) {
    return status == ReaderStatus::Online ? "online" : "offline";
}

// A reader stopped heartbeating (Offline), or started again (Online).
struct ReaderStatusEvent {
    core::TimePoint timestamp;
    core::ReaderId readerId;
    ReaderStatus status = ReaderStatus::Offline;

    friend bool operator==(const ReaderStatusEvent&, const ReaderStatusEvent&) = default;
};

}  // namespace acs::events
