#pragma once

#include "core/time.h"

namespace acs::core {

// Source of "now". Production code uses SystemClock; tests pass fixed times so
// time-dependent behaviour is deterministic.
class IClock {
public:
    virtual ~IClock() = default;
    [[nodiscard]] virtual TimePoint now() const = 0;
};

}  // namespace acs::core
