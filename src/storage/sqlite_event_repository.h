#pragma once

#include <cstddef>
#include <vector>

#include "events/event_repository.h"
#include "storage/database.h"

namespace acs::storage {

// Lifetime: `db` must outlive the repository.
// Threading: used only by the thread that owns `db`. In the server that is
// the AuditLogWriter thread, which has its own connection.
class SqliteEventRepository final : public events::IEventRepository {
public:
    explicit SqliteEventRepository(Database& db);

    // One transaction per batch, whatever mix of event types it holds.
    // Catches StorageError and returns false.
    [[nodiscard]] bool append(std::span<const events::Event> events) override;

    // Newest first. Throws StorageError.
    [[nodiscard]] std::vector<events::AccessEvent> loadRecent(std::size_t limit) const;
    [[nodiscard]] std::vector<events::ReaderStatusEvent> loadRecentReaderStatus(
        std::size_t limit) const;

private:
    Database& m_db;
};

}  // namespace acs::storage
