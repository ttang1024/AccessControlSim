#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "net/messages.h"

namespace acs::net {

// Conversion between JSON payloads and message structs. Frame headers are
// handled separately, in frame_codec.h.
// Bad input is an expected failure, so it comes back as a value and is never
// thrown.

// Server side.
[[nodiscard]] InboundMessage parseInboundMessage(std::string_view payload);
[[nodiscard]] std::string serialize(const AccessResponseMessage& message);

// Reader side.
[[nodiscard]] std::string serialize(const AccessRequestMessage& message);
[[nodiscard]] std::string serialize(const HeartbeatMessage& message);
[[nodiscard]] std::optional<AccessResponseMessage> parseAccessResponse(std::string_view payload);

}  // namespace acs::net
