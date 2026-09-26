#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/ids.h"
#include "core/time.h"

namespace acs::core {

enum class CardholderStatus { Active, Suspended, Expired };

// Lowercase names used in the seed file and the database ("active", ...).
[[nodiscard]] std::string_view toString(CardholderStatus status);
[[nodiscard]] std::optional<CardholderStatus> cardholderStatusFromString(std::string_view name);

struct Cardholder {
    CardholderId id;
    std::string name;
    CardholderStatus status = CardholderStatus::Active;
    // Access ends at this instant (exclusive). No value means no expiry.
    std::optional<TimePoint> expiresAt;
    std::vector<AccessGroupId> accessGroups;

    friend bool operator==(const Cardholder&, const Cardholder&) = default;
};

}  // namespace acs::core
