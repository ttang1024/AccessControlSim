#pragma once

#include <atomic>
#include <cstddef>
#include <thread>

#include "events/event_queue.h"
#include "events/event_repository.h"

namespace acs::events {

// Moves events from the queue into the repository on its own thread, in
// batches. With batching, one SQLite transaction (and one disk sync) covers
// many events instead of one each.
//
// Threading: the writer thread is the only thread that uses the repository
// while the writer is running. Construct, stop() and destroy it from one
// owning thread.
// Lifetime: `queue` and `repository` must outlive the writer.
class AuditLogWriter {
public:
    static constexpr std::size_t kDefaultMaxBatch = 256;

    AuditLogWriter(EventQueue& queue, IEventRepository& repository,
                   std::size_t maxBatch = kDefaultMaxBatch);
    ~AuditLogWriter();

    AuditLogWriter(const AuditLogWriter&) = delete;
    AuditLogWriter& operator=(const AuditLogWriter&) = delete;

    // Closes the queue, writes out everything still queued, and joins the
    // thread. Safe to call more than once.
    void stop();

    // Events the repository failed to store. Safe to read from any thread.
    [[nodiscard]] std::size_t failedEventCount() const { return m_failedEvents.load(); }

private:
    void run();

    EventQueue& m_queue;
    IEventRepository& m_repository;
    const std::size_t m_maxBatch;
    std::atomic<std::size_t> m_failedEvents{0};
    std::jthread m_thread;  // Declared last so it starts after everything it uses.
};

}  // namespace acs::events
