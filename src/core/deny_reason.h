#pragma once

#include <array>
#include <optional>
#include <string_view>

namespace acs::core {

enum class DenyReason {
    UnknownCard,
    CardholderSuspended,
    CardholderExpired,
    NoZoneAccess,
    OutsideSchedule,
    MalformedRequest,
    InternalError,
};

inline constexpr std::array kAllDenyReasons{
    DenyReason::UnknownCard,   DenyReason::CardholderSuspended, DenyReason::CardholderExpired,
    DenyReason::NoZoneAccess,  DenyReason::OutsideSchedule,     DenyReason::MalformedRequest,
    DenyReason::InternalError,
};

// Stable name used on the wire and in the audit log (e.g. "OutsideSchedule").
[[nodiscard]] std::string_view toString(DenyReason reason);

// Inverse of toString. Returns nullopt for names that are not a DenyReason.
[[nodiscard]] std::optional<DenyReason> denyReasonFromString(std::string_view name);

}  // namespace acs::core
