#pragma once

#include <compare>
#include <cstdint>
#include <string>

#include "core/ids.h"

namespace acs::core {

// What a reader actually reads off the badge. The same card number can exist
// under different facility codes, so both fields together identify a card.
struct CardCredential {
    std::string number;
    std::uint32_t facilityCode = 0;

    friend auto operator<=>(const CardCredential&, const CardCredential&) = default;
};

struct Card {
    CardCredential credential;
    CardholderId cardholderId;

    friend bool operator==(const Card&, const Card&) = default;
};

}  // namespace acs::core
