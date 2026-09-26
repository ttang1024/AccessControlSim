#include "net/reader_watchdog.h"

#include <gtest/gtest.h>

#include <thread>
#include <variant>

#include "unit/manual_clock.h"
#include "unit/test_time.h"

namespace acs::net {
namespace {

using namespace std::chrono_literals;

TEST(ReaderWatchdogTest, Timer_ReaderGoesQuiet_PushesOfflineEvent) {
    test::ManualClock clock{test::at(std::chrono::Monday, 9h)};
    events::EventQueue queue(10);
    events::ReaderMonitor monitor({"R-1"}, 10s, 3, clock.now(), queue);
    asio::io_context io;
    ReaderWatchdog watchdog(io, monitor, clock, 1ms);
    watchdog.start();
    std::jthread ioThread([&io] { io.run(); });

    clock.advance(31s);
    const auto batch = queue.popBatch(10);  // Blocks until the watchdog pushes.
    io.stop();

    ASSERT_EQ(batch.size(), 1U);
    const auto* event = std::get_if<events::ReaderStatusEvent>(&batch[0]);
    ASSERT_NE(event, nullptr);
    EXPECT_EQ(event->readerId, "R-1");
    EXPECT_EQ(event->status, events::ReaderStatus::Offline);
}

}  // namespace
}  // namespace acs::net
