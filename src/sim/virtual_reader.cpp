#include "sim/virtual_reader.h"

#include <utility>

#include "net/frame_codec.h"
#include "net/message_codec.h"

namespace acs::sim {

VirtualReader::VirtualReader(core::ReaderId id) : m_id(std::move(id)) {}

std::error_code VirtualReader::connect(const std::string& host, std::uint16_t port) {
    std::error_code ec;
    asio::ip::tcp::resolver resolver(m_io);
    const auto endpoints = resolver.resolve(host, std::to_string(port), ec);
    if (ec) {
        return ec;
    }
    asio::connect(m_socket, endpoints, ec);
    if (!ec) {
        m_socket.set_option(asio::ip::tcp::no_delay(true), ec);
    }
    return ec;
}

std::optional<engine::Decision> VirtualReader::swipe(const core::CardCredential& card) {
    const std::uint64_t seq = m_nextSeq++;
    const net::AccessRequestMessage request{.seq = seq,
                                            .request = {.readerId = m_id, .card = card}};
    if (!sendFrame(net::serialize(request))) {
        return std::nullopt;
    }

    const auto payload = receiveFrame();
    if (!payload) {
        return std::nullopt;
    }
    const auto response = net::parseAccessResponse(*payload);
    if (!response || response->seq != seq) {
        return std::nullopt;
    }
    return response->decision;
}

bool VirtualReader::sendHeartbeat() {
    return sendFrame(net::serialize(net::HeartbeatMessage{.readerId = m_id}));
}

bool VirtualReader::sendFrame(std::string_view payload) {
    return sendBytes(net::encodeFrame(payload));
}

bool VirtualReader::sendBytes(std::string_view bytes) {
    std::error_code ec;
    asio::write(m_socket, asio::buffer(bytes), ec);
    return !ec;
}

std::optional<std::string> VirtualReader::receiveFrame() {
    std::error_code ec;
    net::FrameHeader header{};
    asio::read(m_socket, asio::buffer(header), ec);
    if (ec) {
        return std::nullopt;
    }
    const std::uint32_t size = net::decodeFrameHeader(header);
    if (size > net::kMaxPayloadSize) {
        return std::nullopt;
    }
    std::string payload(size, '\0');
    asio::read(m_socket, asio::buffer(payload), ec);
    if (ec) {
        return std::nullopt;
    }
    return payload;
}

}  // namespace acs::sim
