#pragma once

#include <asio.hpp>
#include <chrono>

#include "core/clock.h"
#include "events/reader_monitor.h"

namespace acs::net {

// Calls ReaderMonitor::checkTimeouts on a steady timer and logs each reader
// that goes offline. The monitor itself queues the audit events.
// A steady_timer drives the checks, so changes to the wall clock don't alter
// the check rate. `clock` provides the timestamps, so tests can control time.
// Threading: the timer handler runs on an io_context thread, and there is only
// ever one pending wait. Stop it by stopping the io_context.
// Lifetime: all references must outlive the io_context's threads.
class ReaderWatchdog {
public:
    ReaderWatchdog(asio::io_context& io, events::ReaderMonitor& readers, const core::IClock& clock,
                   std::chrono::milliseconds checkPeriod);

    void start();

private:
    void scheduleCheck();

    asio::steady_timer m_timer;
    events::ReaderMonitor& m_readers;
    const core::IClock& m_clock;
    const std::chrono::milliseconds m_checkPeriod;
};

}  // namespace acs::net
