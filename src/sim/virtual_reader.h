#pragma once

#include <asio.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "core/card.h"
#include "core/ids.h"
#include "engine/decision.h"

namespace acs::sim {

// A simulated card reader with one blocking TCP connection to the server.
// Blocking I/O keeps the simulator and integration tests simple and linear.
// It is a test driver, not a production path, so the async machinery isn't needed.
// Threading: each instance is used by one thread only.
class VirtualReader {
public:
    explicit VirtualReader(core::ReaderId id);

    [[nodiscard]] std::error_code connect(const std::string& host, std::uint16_t port);

    // Sends a badge swipe and waits for the response with the matching seq.
    // Returns nullopt on an I/O error or a response that can't be understood.
    [[nodiscard]] std::optional<engine::Decision> swipe(const core::CardCredential& card);

    [[nodiscard]] bool sendHeartbeat();

    // Lower-level access, used by tests to send malformed input.
    [[nodiscard]] bool sendFrame(std::string_view payload);
    [[nodiscard]] bool sendBytes(std::string_view bytes);
    // Returns nullopt if the connection closes or fails.
    [[nodiscard]] std::optional<std::string> receiveFrame();

private:
    core::ReaderId m_id;
    std::uint64_t m_nextSeq = 1;
    asio::io_context m_io;
    asio::ip::tcp::socket m_socket{m_io};
};

}  // namespace acs::sim
