#include "events/event_queue.h"

#include <gtest/gtest.h>

#include <vector>

#include "unit/test_time.h"

namespace acs::events {
namespace {

using namespace std::chrono_literals;

Event offlineAt(core::TimePoint at) {
    return ReaderStatusEvent{.timestamp = at, .readerId = "R-1", .status = ReaderStatus::Offline};
}

TEST(PushEventTest, PushEvent_QueueHasRoom_EventIsQueued) {
    EventQueue queue(10);
    const Event event = offlineAt(test::at(std::chrono::Monday, 9h));

    pushEvent(queue, event);

    queue.close();
    EXPECT_EQ(queue.popBatch(10), std::vector<Event>{event});
    EXPECT_EQ(queue.droppedCount(), 0U);
}

// pushEvent is called from I/O threads, so a full queue must drop the event
// and return, not block or throw (ADR-006).
TEST(PushEventTest, PushEvent_QueueFull_DropsNewestAndCountsIt) {
    EventQueue queue(1);
    const Event first = offlineAt(test::at(std::chrono::Monday, 9h));

    pushEvent(queue, first);
    pushEvent(queue, offlineAt(test::at(std::chrono::Monday, 10h)));
    pushEvent(queue, offlineAt(test::at(std::chrono::Monday, 11h)));

    EXPECT_EQ(queue.droppedCount(), 2U);
    queue.close();
    EXPECT_EQ(queue.popBatch(10), std::vector<Event>{first});
}

}  // namespace
}  // namespace acs::events
