#include "events/reader_monitor.h"

namespace acs::events {

ReaderMonitor::ReaderMonitor(const std::vector<core::ReaderId>& readers,
                             std::chrono::seconds heartbeatInterval, unsigned missedLimit,
                             core::TimePoint start, EventQueue& events)
    : m_offlineAfter(heartbeatInterval * missedLimit), m_events(events) {
    for (const core::ReaderId& id : readers) {
        m_readers.emplace(id, ReaderState{.lastHeartbeat = start, .online = true});
    }
}

bool ReaderMonitor::recordHeartbeat(const core::ReaderId& readerId, core::TimePoint now) {
    const std::scoped_lock lock(m_mutex);
    const auto it = m_readers.find(readerId);
    if (it == m_readers.end()) {
        return false;
    }
    it->second.lastHeartbeat = now;
    if (it->second.online) {
        return false;
    }
    it->second.online = true;
    // Queued under m_mutex: see the lock-order note in the header.
    pushEvent(
        m_events,
        ReaderStatusEvent{.timestamp = now, .readerId = readerId, .status = ReaderStatus::Online});
    return true;
}

std::vector<core::ReaderId> ReaderMonitor::checkTimeouts(core::TimePoint now) {
    const std::scoped_lock lock(m_mutex);
    std::vector<core::ReaderId> wentOffline;
    for (auto& [id, state] : m_readers) {
        if (state.online && now - state.lastHeartbeat > m_offlineAfter) {
            state.online = false;
            // Queued under m_mutex: see the lock-order note in the header.
            pushEvent(m_events,
                      ReaderStatusEvent{
                          .timestamp = now, .readerId = id, .status = ReaderStatus::Offline});
            wentOffline.push_back(id);
        }
    }
    return wentOffline;
}

bool ReaderMonitor::isOnline(const core::ReaderId& readerId) const {
    const std::scoped_lock lock(m_mutex);
    const auto it = m_readers.find(readerId);
    return it != m_readers.end() && it->second.online;
}

}  // namespace acs::events
