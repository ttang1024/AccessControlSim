#pragma once

#include <asio.hpp>
#include <chrono>
#include <cstdint>

#include "net/session.h"

namespace acs::net {

// Listens for reader connections and starts a Session for each one.
//
// Threading: construct and start() on one thread, then have any number of
// threads call io_context::run(). Only one accept is pending at a time, so the
// accept handler never runs twice at once.
// Lifetime: stop the io_context and join its threads before destroying the
// Server, because pending accept handlers capture `this`.
class Server {
public:
    // Binds right away. Throws std::system_error if the endpoint can't be bound,
    // since that is a startup failure. `idleTimeout` is passed to each Session.
    Server(asio::io_context& io, const asio::ip::tcp::endpoint& endpoint, MessageHandler handler,
           std::chrono::milliseconds idleTimeout);

    // The bound port, which is useful when binding to port 0 in tests.
    [[nodiscard]] std::uint16_t port() const;

    void start();

private:
    void accept();

    asio::ip::tcp::acceptor m_acceptor;
    // Used only after a failed accept, to wait before retrying.
    asio::steady_timer m_retryTimer;
    MessageHandler m_handler;
    const std::chrono::milliseconds m_idleTimeout;
};

}  // namespace acs::net
