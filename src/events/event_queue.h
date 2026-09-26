#pragma once

#include "events/bounded_queue.h"
#include "events/event.h"

namespace acs::events {

// Carries audit events from the network threads to the AuditLogWriter.
using EventQueue = BoundedQueue<Event>;

// Pushes an event without blocking. If the queue is full the event is dropped
// (ADR-006), and a warning is logged at the 1st, 10th, 100th, ... drop, so an
// overloaded server doesn't also flood its log.
void pushEvent(EventQueue& queue, Event event);

}  // namespace acs::events
