#pragma once

#include <asio.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "net/frame_codec.h"

namespace acs::net {

// Takes one payload and returns the reply payload, or nullopt for no reply.
// It is called from I/O threads, so it must be thread-safe.
using MessageHandler = std::function<std::optional<std::string>(std::string_view payload)>;

// One reader connection. It loops: read a frame, handle it, write the reply,
// then read the next frame. Replies go out in request order.
//
// Idle timeout: each time the session starts waiting for a frame it gives the
// peer `idleTimeout` to deliver it (and for the reply to be written). A peer
// that vanished without closing the socket (power loss, pulled cable) or
// stalls mid-frame is disconnected, so its socket and Session are freed
// (ADR-012).
//
// Threading: at most one I/O operation and one timer wait are pending. Both
// run on this session's strand, and start() hops onto it first, so no two
// handlers of one session ever run at the same time, even with several
// threads running the io_context. m_deadline is only touched on the strand.
//
// Lifetime: the shared_ptr is captured by each pending handler. The session is
// destroyed once the connection closes and the last handler returns.
class Session : public std::enable_shared_from_this<Session> {
public:
    // `socket` must already be bound to a strand (Server does this).
    Session(asio::ip::tcp::socket socket, MessageHandler handler,
            std::chrono::milliseconds idleTimeout);

    void start();

private:
    void waitForIdleDeadline();
    void readHeader();
    void readPayload(std::uint32_t size);
    void handlePayload();
    void writeResponse(std::string payload);
    void close();

    asio::ip::tcp::socket m_socket;
    std::string m_peer;  // "address:port", for log lines.
    MessageHandler m_handler;
    const std::chrono::milliseconds m_idleTimeout;
    asio::steady_timer m_idleTimer;
    std::chrono::steady_clock::time_point m_deadline;  // Close if no frame by then.
    FrameHeader m_header{};
    std::string m_payload;
    std::string m_outbound;
};

}  // namespace acs::net
