#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>

namespace acs::events {

// A fixed-capacity FIFO queue for many producers and one consumer.
// Producers never block. When the queue is full, tryPush drops the item and
// counts it, so a slow consumer (e.g. a busy disk) can't hold up the network
// threads (see ADR-006).
// Thread safety: every member function can be called from any thread.
template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : m_capacity(capacity) {}

    // Adds the item unless the queue is full or closed. Never blocks.
    // Returns false, and counts a drop, if the item was not added.
    [[nodiscard]] bool tryPush(T item) {
        {
            const std::scoped_lock lock(m_mutex);
            if (m_closed || m_items.size() >= m_capacity) {
                ++m_dropped;
                return false;
            }
            m_items.push_back(std::move(item));
        }
        m_notEmpty.notify_one();
        return true;
    }

    // Blocks until there is at least one item or the queue is closed, then
    // takes up to `maxItems` (at least 1) in FIFO order. Returns an empty
    // vector only when the queue is closed and fully drained.
    [[nodiscard]] std::vector<T> popBatch(std::size_t maxItems) {
        std::unique_lock lock(m_mutex);
        m_notEmpty.wait(lock, [this] { return m_closed || !m_items.empty(); });

        // An empty batch means "closed", so never return one just because maxItems is 0.
        const std::size_t limit = std::max<std::size_t>(maxItems, 1);
        std::vector<T> batch;
        while (!m_items.empty() && batch.size() < limit) {
            batch.push_back(std::move(m_items.front()));
            m_items.pop_front();
        }
        return batch;
    }

    // Stops accepting new items and wakes the consumer. Items already queued
    // can still be popped.
    void close() {
        {
            const std::scoped_lock lock(m_mutex);
            m_closed = true;
        }
        m_notEmpty.notify_all();
    }

    [[nodiscard]] std::size_t droppedCount() const {
        const std::scoped_lock lock(m_mutex);
        return m_dropped;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_notEmpty;
    std::deque<T> m_items;
    const std::size_t m_capacity;
    std::size_t m_dropped = 0;
    bool m_closed = false;
};

}  // namespace acs::events
