#include "core/cardholder.h"

namespace acs::core {

std::string_view toString(CardholderStatus status) {
    switch (status) {
        case CardholderStatus::Active:
            return "active";
        case CardholderStatus::Suspended:
            return "suspended";
        case CardholderStatus::Expired:
            return "expired";
    }
    // Only reachable if an out-of-range value is cast into the enum.
    return "expired";
}

std::optional<CardholderStatus> cardholderStatusFromString(std::string_view name) {
    for (const auto status :
         {CardholderStatus::Active, CardholderStatus::Suspended, CardholderStatus::Expired}) {
        if (toString(status) == name) {
            return status;
        }
    }
    return std::nullopt;
}

}  // namespace acs::core
