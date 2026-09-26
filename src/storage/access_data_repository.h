#pragma once

#include "engine/access_snapshot.h"

namespace acs::storage {

// Reads and writes the site data (zones, readers, schedules, groups,
// cardholders, cards) as one whole snapshot. The only reader is startup, which
// builds the engine's immutable snapshot, so per-entity repositories would have
// no callers yet (see ADR-007).
// Both methods throw StorageError on failure: they run at startup or during
// admin work, where a failure really is exceptional.
class IAccessDataRepository {
public:
    virtual ~IAccessDataRepository() = default;

    [[nodiscard]] virtual engine::AccessSnapshot loadSnapshot() const = 0;

    // Replaces all site data in one transaction. The audit log is not touched.
    virtual void replaceAll(const engine::AccessSnapshot& snapshot) = 0;
};

}  // namespace acs::storage
