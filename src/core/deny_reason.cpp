#include "core/deny_reason.h"

namespace acs::core {

std::string_view toString(DenyReason reason) {
    switch (reason) {
        case DenyReason::UnknownCard:
            return "UnknownCard";
        case DenyReason::CardholderSuspended:
            return "CardholderSuspended";
        case DenyReason::CardholderExpired:
            return "CardholderExpired";
        case DenyReason::NoZoneAccess:
            return "NoZoneAccess";
        case DenyReason::OutsideSchedule:
            return "OutsideSchedule";
        case DenyReason::MalformedRequest:
            return "MalformedRequest";
        case DenyReason::InternalError:
            return "InternalError";
    }
    // Only reachable if an out-of-range value is cast into the enum.
    return "InternalError";
}

std::optional<DenyReason> denyReasonFromString(std::string_view name) {
    for (const DenyReason reason : kAllDenyReasons) {
        if (toString(reason) == name) {
            return reason;
        }
    }
    return std::nullopt;
}

}  // namespace acs::core
