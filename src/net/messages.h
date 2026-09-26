#pragma once

#include <cstdint>
#include <optional>
#include <variant>

#include "core/ids.h"
#include "engine/access_request.h"
#include "engine/decision.h"

namespace acs::net {

// In-memory forms of the wire messages in docs/protocol.md.

struct AccessRequestMessage {
    std::uint64_t seq = 0;
    engine::AccessRequest request;
};

struct HeartbeatMessage {
    core::ReaderId readerId;
};

// A payload the server could not understand. `seq` is kept when it could be
// read, so the reader can still match the deny to its request.
struct MalformedMessage {
    std::optional<std::uint64_t> seq;
};

using InboundMessage = std::variant<AccessRequestMessage, HeartbeatMessage, MalformedMessage>;

struct AccessResponseMessage {
    std::optional<std::uint64_t> seq;
    engine::Decision decision;
};

}  // namespace acs::net
