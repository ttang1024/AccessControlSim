#include "events/event_queue.h"

#include <spdlog/spdlog.h>

#include <utility>

namespace acs::events {

namespace {

bool isPowerOfTen(std::size_t n) {
    while (n >= 10 && n % 10 == 0) {
        n /= 10;
    }
    return n == 1;
}

}  // namespace

void pushEvent(EventQueue& queue, Event event) {
    if (!queue.tryPush(std::move(event))) {
        const std::size_t dropped = queue.droppedCount();
        if (isPowerOfTen(dropped)) {
            spdlog::warn("event=audit_event_dropped reason=queue_full total_dropped={}", dropped);
        }
    }
}

}  // namespace acs::events
