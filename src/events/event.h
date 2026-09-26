#pragma once

#include <variant>

#include "events/access_event.h"
#include "events/reader_status_event.h"

namespace acs::events {

// Everything that goes to the audit log. A variant, not a base class, because
// the set of event types is closed and small, and std::visit makes each
// consumer handle every type.
using Event = std::variant<AccessEvent, ReaderStatusEvent>;

}  // namespace acs::events
