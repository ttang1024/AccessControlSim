#pragma once

#include "core/card.h"
#include "core/ids.h"
#include "core/time.h"
#include "engine/decision.h"

namespace acs::events {

// One audit-log entry: who swiped where, when, and what was decided.
struct AccessEvent {
    core::TimePoint timestamp;
    // Both empty when the request was malformed and these fields couldn't be read.
    core::ReaderId readerId;
    core::CardCredential card;
    engine::Decision decision;

    friend bool operator==(const AccessEvent&, const AccessEvent&) = default;
};

}  // namespace acs::events
