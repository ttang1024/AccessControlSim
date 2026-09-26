#pragma once

#include <span>

#include "events/event.h"

namespace acs::events {

// Where audit events end up. The interface lives here, next to its consumer
// (AuditLogWriter), so events/ doesn't depend on storage/. storage/ implements
// it with SQLite.
class IEventRepository {
public:
    virtual ~IEventRepository() = default;

    // Stores every event, or none of them. Returns false if nothing was stored.
    // Expected runtime failures, such as a full disk, return false instead of throwing.
    [[nodiscard]] virtual bool append(std::span<const Event> events) = 0;
};

}  // namespace acs::events
