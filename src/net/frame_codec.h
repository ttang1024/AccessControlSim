#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace acs::net {

// Wire framing: a 4-byte big-endian payload length, then the payload.
// See docs/protocol.md.
inline constexpr std::size_t kFrameHeaderSize = 4;
// Largest payload accepted, not counting the header. Bigger frames close the connection.
inline constexpr std::uint32_t kMaxPayloadSize = 64 * 1024;

using FrameHeader = std::array<std::uint8_t, kFrameHeaderSize>;

[[nodiscard]] FrameHeader encodeFrameHeader(std::uint32_t payloadSize);
[[nodiscard]] std::uint32_t decodeFrameHeader(const FrameHeader& header);

// Header plus payload in one buffer, so a frame goes out in a single write.
// The payload must be at most kMaxPayloadSize bytes.
[[nodiscard]] std::string encodeFrame(std::string_view payload);

}  // namespace acs::net
