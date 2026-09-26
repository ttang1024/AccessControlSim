#pragma once

#include "core/time.h"
#include "engine/access_request.h"
#include "engine/access_snapshot.h"
#include "engine/decision.h"

namespace acs::engine {

// Pure decision logic: the same inputs always give the same Decision.
// It does no I/O, logging or locking and never reads a clock; the caller passes
// `now`, usually from an IClock.
// Thread safety: stateless and const, so any thread can call it at the same time.
class AccessDecisionEngine {
public:
    [[nodiscard]] Decision decide(const AccessRequest& request, const AccessSnapshot& snapshot,
                                  core::TimePoint now) const;
};

}  // namespace acs::engine
