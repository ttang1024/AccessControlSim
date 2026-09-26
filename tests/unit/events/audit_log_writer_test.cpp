#include "events/audit_log_writer.h"

#include <gtest/gtest.h>

#include <vector>

#include "unit/test_time.h"

namespace acs::events {
namespace {

using namespace std::chrono_literals;

// Records what it was given. The writer thread is the only caller while the
// writer runs, and tests read the results only after stop() has joined that
// thread, so no locking is needed.
class FakeEventRepository final : public IEventRepository {
public:
    bool append(std::span<const Event> events) override {
        batchSizes.push_back(events.size());
        if (fail) {
            return false;
        }
        stored.insert(stored.end(), events.begin(), events.end());
        return true;
    }

    bool fail = false;
    std::vector<Event> stored;
    std::vector<std::size_t> batchSizes;
};

AccessEvent makeEvent(int n) {
    return AccessEvent{.timestamp = test::at(std::chrono::Monday, 9h) + std::chrono::seconds{n},
                       .readerId = "R-1",
                       .card = {.number = std::to_string(n), .facilityCode = 42},
                       .decision = engine::Decision::grant()};
}

TEST(AuditLogWriterTest, Stop_AfterEventsQueued_WritesAllInOrder) {
    EventQueue queue(100);
    FakeEventRepository repository;
    AuditLogWriter writer(queue, repository);

    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(queue.tryPush(makeEvent(i)));
    }
    writer.stop();

    ASSERT_EQ(repository.stored.size(), 10U);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(repository.stored[static_cast<std::size_t>(i)], Event{makeEvent(i)});
    }
    EXPECT_EQ(writer.failedEventCount(), 0U);
}

TEST(AuditLogWriterTest, Run_MixedEventTypes_AllReachRepository) {
    EventQueue queue(10);
    FakeEventRepository repository;
    const ReaderStatusEvent offline{.timestamp = test::at(std::chrono::Monday, 9h),
                                    .readerId = "R-1",
                                    .status = ReaderStatus::Offline};
    ASSERT_TRUE(queue.tryPush(makeEvent(1)));
    ASSERT_TRUE(queue.tryPush(offline));
    AuditLogWriter writer(queue, repository);
    writer.stop();

    EXPECT_EQ(repository.stored, (std::vector<Event>{makeEvent(1), offline}));
}

TEST(AuditLogWriterTest, Run_ManyEventsQueued_BatchesNeverExceedMax) {
    EventQueue queue(100);
    for (int i = 0; i < 25; ++i) {
        ASSERT_TRUE(queue.tryPush(makeEvent(i)));
    }
    FakeEventRepository repository;
    AuditLogWriter writer(queue, repository, /*maxBatch=*/4);
    writer.stop();

    EXPECT_EQ(repository.stored.size(), 25U);
    for (const std::size_t size : repository.batchSizes) {
        EXPECT_LE(size, 4U);
    }
}

TEST(AuditLogWriterTest, Run_RepositoryFails_CountsFailedEvents) {
    EventQueue queue(100);
    FakeEventRepository repository;
    repository.fail = true;
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(queue.tryPush(makeEvent(i)));
    }
    AuditLogWriter writer(queue, repository);
    writer.stop();

    EXPECT_EQ(writer.failedEventCount(), 3U);
}

TEST(AuditLogWriterTest, Stop_CalledTwice_IsSafe) {
    EventQueue queue(10);
    FakeEventRepository repository;
    AuditLogWriter writer(queue, repository);
    writer.stop();
    writer.stop();  // And the destructor calls it a third time.
    SUCCEED();
}

}  // namespace
}  // namespace acs::events
