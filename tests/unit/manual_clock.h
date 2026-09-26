#pragma once

#include <atomic>

#include "core/clock.h"

namespace acs::test {

// An IClock that tests move forward by hand. It is atomic, so a test thread
// can advance it while server threads read it.
class ManualClock final : public core::IClock {
public:
    explicit ManualClock(core::TimePoint start) : m_time(start) {}

    [[nodiscard]] core::TimePoint now() const override { return m_time.load(); }
    void advance(core::TimePoint::duration by) { m_time.store(m_time.load() + by); }

private:
    std::atomic<core::TimePoint> m_time;
};

}  // namespace acs::test
