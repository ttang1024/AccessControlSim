#include "events/reader_monitor.h"

#include <gtest/gtest.h>

#include <thread>

#include "unit/test_time.h"

namespace acs::events {
namespace {

using namespace std::chrono_literals;

// Heartbeat every 10 s; offline after more than 3 missed, i.e. more than 30 s of silence.
class ReaderMonitorTest : public ::testing::Test {
protected:
    // Events queued so far. The queue is closed first, so this never blocks.
    std::vector<Event> queuedEvents() {
        m_events.close();
        return m_events.popBatch(1000);
    }

    static Event status(core::TimePoint at, const core::ReaderId& id, ReaderStatus s) {
        return ReaderStatusEvent{.timestamp = at, .readerId = id, .status = s};
    }

    const core::TimePoint m_start = test::at(std::chrono::Monday, 9h);
    EventQueue m_events{1000};
    ReaderMonitor m_monitor{{"R-1", "R-2"}, 10s, 3, m_start, m_events};
};

TEST_F(ReaderMonitorTest, CheckTimeouts_AtExactlyThreeIntervals_StillOnline) {
    EXPECT_TRUE(m_monitor.checkTimeouts(m_start + 30s).empty());
    EXPECT_TRUE(m_monitor.isOnline("R-1"));
    EXPECT_TRUE(queuedEvents().empty());
}

TEST_F(ReaderMonitorTest, CheckTimeouts_JustOverThreeIntervals_MarksAndQueuesEveryQuietReader) {
    const auto at = m_start + 30s + 1ms;

    EXPECT_EQ(m_monitor.checkTimeouts(at), (std::vector<core::ReaderId>{"R-1", "R-2"}));
    EXPECT_FALSE(m_monitor.isOnline("R-1"));
    EXPECT_EQ(queuedEvents(), (std::vector{status(at, "R-1", ReaderStatus::Offline),
                                           status(at, "R-2", ReaderStatus::Offline)}));
}

TEST_F(ReaderMonitorTest, CheckTimeouts_ReaderAlreadyOffline_NotReportedAgain) {
    ASSERT_EQ(m_monitor.checkTimeouts(m_start + 31s).size(), 2U);
    EXPECT_TRUE(m_monitor.checkTimeouts(m_start + 60s).empty());
    EXPECT_EQ(queuedEvents().size(), 2U);
}

TEST_F(ReaderMonitorTest, RecordHeartbeat_KeepsReaderOnlinePastOriginalDeadline) {
    EXPECT_FALSE(m_monitor.recordHeartbeat("R-1", m_start + 25s));

    // Only R-2 went quiet. R-1's deadline moved to 25 s + 30 s.
    EXPECT_EQ(m_monitor.checkTimeouts(m_start + 40s), std::vector<core::ReaderId>{"R-2"});
    EXPECT_TRUE(m_monitor.isOnline("R-1"));
}

TEST_F(ReaderMonitorTest, RecordHeartbeat_OfflineReader_MarksOnlineAndQueuesEvent) {
    (void)m_monitor.checkTimeouts(m_start + 31s);

    EXPECT_TRUE(m_monitor.recordHeartbeat("R-1", m_start + 45s));

    EXPECT_TRUE(m_monitor.isOnline("R-1"));
    EXPECT_EQ(queuedEvents(), (std::vector{status(m_start + 31s, "R-1", ReaderStatus::Offline),
                                           status(m_start + 31s, "R-2", ReaderStatus::Offline),
                                           status(m_start + 45s, "R-1", ReaderStatus::Online)}));
}

TEST_F(ReaderMonitorTest, RecordHeartbeat_BackOnline_CanGoOfflineAgain) {
    (void)m_monitor.checkTimeouts(m_start + 31s);
    (void)m_monitor.recordHeartbeat("R-1", m_start + 45s);

    EXPECT_EQ(m_monitor.checkTimeouts(m_start + 76s), std::vector<core::ReaderId>{"R-1"});
}

TEST_F(ReaderMonitorTest, RecordHeartbeat_UnknownReader_IsIgnored) {
    EXPECT_FALSE(m_monitor.recordHeartbeat("R-999", m_start + 1s));
    EXPECT_FALSE(m_monitor.isOnline("R-999"));
    EXPECT_EQ(m_monitor.checkTimeouts(m_start + 31s).size(), 2U);
}

// The bug this design fixes: a heartbeat racing the watchdog must never leave
// the audit log ending on "offline" for a reader that is online. Hammer both
// paths from two threads, then check that each reader's events alternate
// strictly and agree with the final state. Run under TSan too.
TEST(ReaderMonitorConcurrencyTest, HeartbeatsRacingTimeouts_EventOrderMatchesFinalState) {
    const core::TimePoint start = test::at(std::chrono::Monday, 9h);
    EventQueue events(1'000'000);
    ReaderMonitor monitor({"R-1"}, 1s, 1, start, events);

    constexpr int kRounds = 20'000;
    std::jthread watchdog([&] {
        for (int i = 0; i < kRounds; ++i) {
            (void)monitor.checkTimeouts(start + std::chrono::hours{1});  // Always "too quiet".
        }
    });
    std::jthread heartbeats([&] {
        for (int i = 0; i < kRounds; ++i) {
            (void)monitor.recordHeartbeat("R-1", start);
        }
    });
    watchdog.join();
    heartbeats.join();

    events.close();
    const auto queued = events.popBatch(1'000'000);
    ASSERT_FALSE(queued.empty());
    ReaderStatus expected = ReaderStatus::Offline;  // The reader starts online.
    for (const Event& event : queued) {
        const auto& change = std::get<ReaderStatusEvent>(event);
        ASSERT_EQ(change.status, expected) << "events out of order";
        expected = expected == ReaderStatus::Offline ? ReaderStatus::Online : ReaderStatus::Offline;
    }
    const auto last = std::get<ReaderStatusEvent>(queued.back()).status;
    EXPECT_EQ(last == ReaderStatus::Online, monitor.isOnline("R-1"));
}

}  // namespace
}  // namespace acs::events
