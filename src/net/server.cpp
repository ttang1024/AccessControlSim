#include "net/server.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <memory>
#include <utility>

namespace acs::net {

Server::Server(asio::io_context& io, const asio::ip::tcp::endpoint& endpoint,
               MessageHandler handler, std::chrono::milliseconds idleTimeout)
    : m_acceptor(io, endpoint),
      m_retryTimer(io),
      m_handler(std::move(handler)),
      m_idleTimeout(idleTimeout) {}

std::uint16_t Server::port() const { return m_acceptor.local_endpoint().port(); }

void Server::start() { accept(); }

void Server::accept() {
    // Each accepted socket gets its own strand. See the note in session.h.
    m_acceptor.async_accept(
        asio::make_strand(m_acceptor.get_executor()),
        [this](std::error_code ec, asio::ip::tcp::socket socket) {
            if (ec == asio::error::operation_aborted) {
                return;  // Acceptor closed: shutting down.
            }
            if (!ec) {
                std::make_shared<Session>(std::move(socket), m_handler, m_idleTimeout)->start();
                accept();
                return;
            }
            // Failures such as running out of file descriptors
            // usually last a while. Retrying at once would spin
            // the CPU, so wait a moment, but keep listening.
            spdlog::error("event=accept_failed error=\"{}\"", ec.message());
            m_retryTimer.expires_after(std::chrono::milliseconds{100});
            m_retryTimer.async_wait([this](std::error_code timerEc) {
                if (!timerEc) {
                    accept();
                }
            });
        });
}

}  // namespace acs::net
