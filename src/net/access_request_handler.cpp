#include "net/access_request_handler.h"

#include <spdlog/spdlog.h>

#include <utility>
#include <variant>

#include "core/overloaded.h"
#include "net/message_codec.h"

namespace acs::net {

AccessRequestHandler::AccessRequestHandler(std::shared_ptr<const engine::AccessSnapshot> snapshot,
                                           const core::IClock& clock, events::EventQueue& events,
                                           events::ReaderMonitor& readers)
    : m_snapshot(std::move(snapshot)), m_clock(clock), m_events(events), m_readers(readers) {}

std::optional<std::string> AccessRequestHandler::handle(std::string_view payload) const {
    return std::visit(
        core::Overloaded{
            [&](const AccessRequestMessage& msg) -> std::optional<std::string> {
                const core::TimePoint now = m_clock.now();
                const auto decision = m_engine.decide(msg.request, *m_snapshot, now);
                recordAccess(now, msg.request, decision);
                return serialize(AccessResponseMessage{.seq = msg.seq, .decision = decision});
            },
            [&](const HeartbeatMessage& msg) -> std::optional<std::string> {
                recordHeartbeat(msg.readerId);
                return std::nullopt;
            },
            [&](const MalformedMessage& msg) -> std::optional<std::string> {
                const auto decision = engine::Decision::deny(core::DenyReason::MalformedRequest);
                // Reader and card are unknown here, so the event records them as empty.
                recordAccess(m_clock.now(), engine::AccessRequest{}, decision);
                return serialize(AccessResponseMessage{.seq = msg.seq, .decision = decision});
            },
        },
        parseInboundMessage(payload));
}

void AccessRequestHandler::recordAccess(core::TimePoint now, const engine::AccessRequest& request,
                                        const engine::Decision& decision) const {
    // At debug level: under load this runs thousands of times a second, and
    // the audit log already keeps the permanent record.
    if (decision.isGranted()) {
        spdlog::debug("event=access_granted reader={} card={} facility={}", request.readerId,
                      request.card.number, request.card.facilityCode);
    } else {
        spdlog::debug("event=access_denied reader={} card={} facility={} reason={}",
                      request.readerId, request.card.number, request.card.facilityCode,
                      core::toString(*decision.denyReason()));
    }
    // The door decision has already been made whatever happens to the event.
    events::pushEvent(m_events, events::AccessEvent{.timestamp = now,
                                                    .readerId = request.readerId,
                                                    .card = request.card,
                                                    .decision = decision});
}

void AccessRequestHandler::recordHeartbeat(const core::ReaderId& readerId) const {
    if (!m_snapshot->readers.contains(readerId)) {
        spdlog::warn("event=heartbeat_ignored reason=unknown_reader reader={}", readerId);
        return;
    }
    if (m_readers.recordHeartbeat(readerId, m_clock.now())) {
        spdlog::info("event=reader_online reader={}", readerId);
    }
}

}  // namespace acs::net
