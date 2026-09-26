#pragma once

#include "core/ids.h"

namespace acs::core {

// A physical card reader. Each reader guards exactly one zone.
struct Reader {
    ReaderId id;
    ZoneId zoneId;

    friend bool operator==(const Reader&, const Reader&) = default;
};

}  // namespace acs::core
