#include "net/reader_watchdog.h"

#include <spdlog/spdlog.h>

namespace acs::net {

ReaderWatchdog::ReaderWatchdog(asio::io_context& io, events::ReaderMonitor& readers,
                               const core::IClock& clock, std::chrono::milliseconds checkPeriod)
    : m_timer(io), m_readers(readers), m_clock(clock), m_checkPeriod(checkPeriod) {}

void ReaderWatchdog::start() { scheduleCheck(); }

void ReaderWatchdog::scheduleCheck() {
    m_timer.expires_after(m_checkPeriod);
    m_timer.async_wait([this](std::error_code ec) {
        if (ec) {
            return;  // operation_aborted: shutting down.
        }
        for (const auto& readerId : m_readers.checkTimeouts(m_clock.now())) {
            spdlog::warn("event=reader_offline reader={}", readerId);
        }
        scheduleCheck();
    });
}

}  // namespace acs::net
