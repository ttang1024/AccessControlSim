#include "net/session.h"

#include <spdlog/spdlog.h>

#include <array>
#include <chrono>
#include <exception>
#include <utility>

namespace acs::net {

Session::Session(asio::ip::tcp::socket socket, MessageHandler handler,
                 std::chrono::milliseconds idleTimeout)
    : m_socket(std::move(socket)),
      m_handler(std::move(handler)),
      m_idleTimeout(idleTimeout),
      // Shares the socket's strand, so timer and I/O handlers never overlap.
      m_idleTimer(m_socket.get_executor()) {
    std::error_code ec;
    const auto endpoint = m_socket.remote_endpoint(ec);
    m_peer =
        ec ? "unknown" : endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
}

void Session::start() {
    spdlog::debug("event=connection_opened peer={}", m_peer);
    // Replies are small and latency-sensitive, so turn off Nagle batching.
    std::error_code ignored;
    m_socket.set_option(asio::ip::tcp::no_delay(true), ignored);
    // start() runs on the acceptor's thread. Hop onto the strand before
    // starting the read and the timer, so their handlers can never run while
    // the other is still being set up.
    asio::dispatch(m_socket.get_executor(), [self = shared_from_this()] {
        self->readHeader();  // Sets the first deadline.
        self->waitForIdleDeadline();
    });
}

// One timer wait covers many frames. readHeader() only moves m_deadline,
// which is cheap. When the timer fires it checks whether the deadline has
// moved since: if so, it waits again until the new deadline, and if not, the
// peer has been idle too long. Re-arming the timer on every frame would add a
// cancel and a new wait to each request.
void Session::waitForIdleDeadline() {
    m_idleTimer.expires_at(m_deadline);
    m_idleTimer.async_wait([self = shared_from_this()](std::error_code ec) {
        if (ec) {
            return;  // operation_aborted: close() cancelled it.
        }
        if (self->m_deadline > std::chrono::steady_clock::now()) {
            return self->waitForIdleDeadline();  // A frame arrived since.
        }
        spdlog::info("event=connection_idle_timeout peer={} timeout_ms={}", self->m_peer,
                     self->m_idleTimeout.count());
        self->close();
    });
}

void Session::readHeader() {
    m_deadline = std::chrono::steady_clock::now() + m_idleTimeout;
    asio::async_read(m_socket, asio::buffer(m_header),
                     [self = shared_from_this()](std::error_code ec, std::size_t /*bytes*/) {
                         if (ec) {
                             return self->close();
                         }
                         const std::uint32_t size = decodeFrameHeader(self->m_header);
                         if (size > kMaxPayloadSize) {
                             // Protocol violation: we can't find the next frame
                             // boundary safely, so drop the connection.
                             spdlog::warn("event=frame_too_large peer={} size={} max={}",
                                          self->m_peer, size, kMaxPayloadSize);
                             return self->close();
                         }
                         self->readPayload(size);
                     });
}

void Session::readPayload(std::uint32_t size) {
    m_payload.resize(size);
    asio::async_read(m_socket, asio::buffer(m_payload),
                     [self = shared_from_this()](std::error_code ec, std::size_t /*bytes*/) {
                         if (ec) {
                             return self->close();
                         }
                         self->handlePayload();
                     });
}

void Session::handlePayload() {
    std::optional<std::string> response;
    try {
        response = m_handler(m_payload);
    } catch (const std::exception& e) {
        // Last resort. An exception escaping an Asio handler would take down
        // an I/O thread. Closing grants nothing, so this is still deny-by-default.
        spdlog::error("event=handler_exception peer={} error=\"{}\"", m_peer, e.what());
        return close();
    }

    if (response) {
        writeResponse(std::move(*response));
    } else {
        readHeader();
    }
}

void Session::writeResponse(std::string payload) {
    // A gather write sends header and payload in one call without first
    // copying them into a combined buffer.
    m_outboundHeader = encodeFrameHeader(static_cast<std::uint32_t>(payload.size()));
    m_outbound = std::move(payload);
    const std::array buffers{asio::buffer(m_outboundHeader), asio::buffer(m_outbound)};
    asio::async_write(m_socket, buffers,
                      [self = shared_from_this()](std::error_code ec, std::size_t /*bytes*/) {
                          if (ec) {
                              return self->close();
                          }
                          self->readHeader();
                      });
}

void Session::close() {
    // Called again when the read cancelled by an idle timeout completes.
    if (!m_socket.is_open()) {
        return;
    }
    spdlog::debug("event=connection_closed peer={}", m_peer);
    // Without this, a pending timer wait would keep the Session alive until
    // the deadline.
    m_idleTimer.cancel();
    std::error_code ignored;
    m_socket.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
    m_socket.close(ignored);
}

}  // namespace acs::net
