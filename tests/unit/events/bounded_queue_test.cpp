#include "events/bounded_queue.h"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

namespace acs::events {
namespace {

TEST(BoundedQueueTest, PopBatch_AfterPushes_ReturnsItemsInFifoOrder) {
    BoundedQueue<int> queue(10);
    ASSERT_TRUE(queue.tryPush(1));
    ASSERT_TRUE(queue.tryPush(2));
    ASSERT_TRUE(queue.tryPush(3));

    EXPECT_EQ(queue.popBatch(10), (std::vector<int>{1, 2, 3}));
}

TEST(BoundedQueueTest, PopBatch_MoreItemsThanMax_ReturnsAtMostMax) {
    BoundedQueue<int> queue(10);
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(queue.tryPush(i));
    }

    EXPECT_EQ(queue.popBatch(2), (std::vector<int>{0, 1}));
    EXPECT_EQ(queue.popBatch(10), (std::vector<int>{2, 3, 4}));
}

TEST(BoundedQueueTest, PopBatch_MaxOfZero_StillReturnsOneItem) {
    BoundedQueue<int> queue(10);
    ASSERT_TRUE(queue.tryPush(7));
    EXPECT_EQ(queue.popBatch(0), (std::vector<int>{7}));
}

TEST(BoundedQueueTest, TryPush_WhenFull_ReturnsFalseAndCountsDrop) {
    BoundedQueue<int> queue(2);
    ASSERT_TRUE(queue.tryPush(1));
    ASSERT_TRUE(queue.tryPush(2));

    EXPECT_FALSE(queue.tryPush(3));
    EXPECT_EQ(queue.droppedCount(), 1U);
    EXPECT_EQ(queue.popBatch(10), (std::vector<int>{1, 2}));
}

TEST(BoundedQueueTest, TryPush_AfterClose_ReturnsFalseAndCountsDrop) {
    BoundedQueue<int> queue(10);
    queue.close();

    EXPECT_FALSE(queue.tryPush(1));
    EXPECT_EQ(queue.droppedCount(), 1U);
}

TEST(BoundedQueueTest, PopBatch_ClosedWithItemsLeft_DrainsThenReturnsEmpty) {
    BoundedQueue<int> queue(10);
    ASSERT_TRUE(queue.tryPush(1));
    queue.close();

    EXPECT_EQ(queue.popBatch(10), (std::vector<int>{1}));
    EXPECT_TRUE(queue.popBatch(10).empty());
}

TEST(BoundedQueueTest, PopBatch_EmptyQueue_BlocksUntilPush) {
    BoundedQueue<int> queue(10);
    std::vector<int> received;
    std::jthread consumer([&] { received = queue.popBatch(10); });

    ASSERT_TRUE(queue.tryPush(42));
    consumer.join();

    EXPECT_EQ(received, (std::vector<int>{42}));
}

TEST(BoundedQueueTest, PopBatch_EmptyQueue_CloseWakesConsumer) {
    BoundedQueue<int> queue(10);
    std::vector<int> received{-1};
    std::jthread consumer([&] { received = queue.popBatch(10); });

    queue.close();
    consumer.join();

    EXPECT_TRUE(received.empty());
}

// Run under ThreadSanitizer to check the queue's locking.
TEST(BoundedQueueTest, ManyProducersOneConsumer_EveryItemDeliveredExactlyOnce) {
    constexpr int kProducers = 8;
    constexpr int kItemsEach = 2000;
    BoundedQueue<int> queue(kProducers * kItemsEach);  // Big enough that nothing drops.

    std::vector<int> seen(kProducers * kItemsEach, 0);
    std::jthread consumer([&] {
        while (true) {
            const auto batch = queue.popBatch(64);
            if (batch.empty()) {
                return;
            }
            for (const int item : batch) {
                ++seen[static_cast<std::size_t>(item)];
            }
        }
    });
    {
        std::vector<std::jthread> producers;
        for (int p = 0; p < kProducers; ++p) {
            producers.emplace_back([&, p] {
                for (int i = 0; i < kItemsEach; ++i) {
                    EXPECT_TRUE(queue.tryPush(p * kItemsEach + i));
                }
            });
        }
    }  // Producers join here.
    queue.close();
    consumer.join();

    EXPECT_EQ(queue.droppedCount(), 0U);
    for (std::size_t i = 0; i < seen.size(); ++i) {
        ASSERT_EQ(seen[i], 1) << "item " << i;
    }
}

}  // namespace
}  // namespace acs::events
