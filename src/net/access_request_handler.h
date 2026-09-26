#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "core/clock.h"
#include "engine/access_decision_engine.h"
#include "engine/access_snapshot.h"
#include "events/event_queue.h"
#include "events/reader_monitor.h"

namespace acs::net {

// Handles one inbound payload from a reader:
//  - access_request: decide, record an AccessEvent, reply.
//  - heartbeat:      update the ReaderMonitor, no reply.
//  - malformed:      record and reply with a MalformedRequest deny.
// It links the protocol to the engine and knows nothing about sockets.
//
// Thread safety: one instance is shared by all sessions on all I/O threads.
// This is safe because the snapshot is immutable, the engine is stateless,
// IClock::now() is const, and EventQueue and ReaderMonitor are thread-safe.
// Lifetime: `clock`, `events` and `readers` must outlive the handler.
class AccessRequestHandler {
public:
    AccessRequestHandler(std::shared_ptr<const engine::AccessSnapshot> snapshot,
                         const core::IClock& clock, events::EventQueue& events,
                         events::ReaderMonitor& readers);

    [[nodiscard]] std::optional<std::string> handle(std::string_view payload) const;

private:
    void recordAccess(core::TimePoint now, const engine::AccessRequest& request,
                      const engine::Decision& decision) const;
    void recordHeartbeat(const core::ReaderId& readerId) const;

    std::shared_ptr<const engine::AccessSnapshot> m_snapshot;
    const core::IClock& m_clock;
    events::EventQueue& m_events;
    events::ReaderMonitor& m_readers;
    engine::AccessDecisionEngine m_engine;
};

}  // namespace acs::net
