#pragma once

#include <chrono>
#include <map>
#include <mutex>
#include <vector>

#include "core/ids.h"
#include "core/time.h"
#include "events/event_queue.h"

namespace acs::events {

// Tracks reader heartbeats and decides when a reader has gone offline.
//
// A reader is offline once more than `missedLimit` heartbeat intervals pass
// with no heartbeat. The test is strictly greater, so a heartbeat arriving
// just on time at exactly 3 intervals does not cause a false offline.
// Every configured reader is monitored from `start`, so a reader that never
// connects after a restart is reported too (ADR-008).
//
// Like the engine, it never reads a clock: callers pass `now`. That makes
// every edge case testable without waiting.
//
// Each status change and its audit event are one atomic step: the event is
// queued while m_mutex is still held. Otherwise a heartbeat on another thread
// could queue ReaderOnline before the watchdog queued the ReaderOffline it
// replaces, and the audit log would end on the wrong status (ADR-011).
// Lock order is always ReaderMonitor -> EventQueue. EventQueue's lock is a
// leaf (the queue never calls out or takes another lock while holding it),
// so this nesting cannot deadlock.
//
// Thread safety: every member function can be called from any thread.
// Heartbeats arrive on I/O threads while the watchdog timer calls checkTimeouts.
// Lifetime: `events` must outlive the monitor.
class ReaderMonitor {
public:
    ReaderMonitor(const std::vector<core::ReaderId>& readers,
                  std::chrono::seconds heartbeatInterval, unsigned missedLimit,
                  core::TimePoint start, EventQueue& events);

    // Records a heartbeat. If the reader had been offline, marks it online,
    // queues a ReaderOnline event, and returns true (so the caller can log it).
    // Unknown readers are ignored.
    [[nodiscard]] bool recordHeartbeat(const core::ReaderId& readerId, core::TimePoint now);

    // Marks readers that have gone quiet for too long offline, queues a
    // ReaderOffline event for each, and returns their IDs (for logging).
    // A reader is reported once, not on every check.
    [[nodiscard]] std::vector<core::ReaderId> checkTimeouts(core::TimePoint now);

    [[nodiscard]] bool isOnline(const core::ReaderId& readerId) const;

private:
    struct ReaderState {
        core::TimePoint lastHeartbeat;
        bool online = true;
    };

    const std::chrono::seconds m_offlineAfter;
    EventQueue& m_events;
    mutable std::mutex m_mutex;
    std::map<core::ReaderId, ReaderState> m_readers;
};

}  // namespace acs::events
