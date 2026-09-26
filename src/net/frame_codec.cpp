#include "net/frame_codec.h"

namespace acs::net {

// Built byte by byte with shifts, so the result is big-endian whatever the
// host's byte order. No htonl() needed.
FrameHeader encodeFrameHeader(std::uint32_t payloadSize) {
    return {
        static_cast<std::uint8_t>(payloadSize >> 24U),
        static_cast<std::uint8_t>(payloadSize >> 16U),
        static_cast<std::uint8_t>(payloadSize >> 8U),
        static_cast<std::uint8_t>(payloadSize),
    };
}

std::uint32_t decodeFrameHeader(const FrameHeader& header) {
    return (static_cast<std::uint32_t>(header[0]) << 24U) |
           (static_cast<std::uint32_t>(header[1]) << 16U) |
           (static_cast<std::uint32_t>(header[2]) << 8U) | static_cast<std::uint32_t>(header[3]);
}

std::string encodeFrame(std::string_view payload) {
    const FrameHeader header = encodeFrameHeader(static_cast<std::uint32_t>(payload.size()));
    std::string frame;
    frame.reserve(kFrameHeaderSize + payload.size());
    for (const std::uint8_t byte : header) {
        frame.push_back(static_cast<char>(byte));
    }
    frame.append(payload);
    return frame;
}

}  // namespace acs::net
