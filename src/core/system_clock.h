#pragma once

#include "core/clock.h"

namespace acs::core {

// The only place in the codebase allowed to read the real wall clock.
// Thread safety: stateless, safe to call from any thread.
class SystemClock final : public IClock {
public:
    [[nodiscard]] TimePoint now() const override;
};

}  // namespace acs::core
