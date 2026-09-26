#include "net/session.h"

#include <gtest/gtest.h>

#include <asio.hpp>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "net/frame_codec.h"

namespace acs::net {
namespace {

using namespace std::chrono_literals;

// Runs one real Session over loopback TCP, driven by a blocking client socket.
// The handler is a stub, so these tests cover only framing, the read/handle/
// write loop, the idle timeout and the Session's lifetime. Protocol handling
// is tested in access_request_handler_test.cpp.
//
// Stub handler: "silent" gets no reply, "throw" throws, anything else gets
// "echo:<payload>".
class SessionTest : public ::testing::Test {
protected:
    ~SessionTest() override {
        m_io.stop();
        m_ioThread = {};  // Joins before the members it uses are destroyed.
    }

    void startSession(std::chrono::milliseconds idleTimeout = 30s) {
        asio::ip::tcp::acceptor acceptor(m_io, {asio::ip::make_address("127.0.0.1"), 0});
        // Completes straight from the listen backlog, so no second thread is needed.
        m_client.connect(acceptor.local_endpoint());
        // A strand for the socket, as Server provides.
        auto session = std::make_shared<Session>(
            acceptor.accept(asio::make_strand(m_io)),
            [this](std::string_view payload) { return handle(payload); }, idleTimeout);
        // Only the Session's own pending handlers keep it alive, as in Server.
        m_session = session;
        session->start();
        m_ioThread = std::jthread([this] { m_io.run(); });
    }

    void send(std::string_view payload) { sendBytes(encodeFrame(payload)); }

    void sendBytes(std::string_view bytes) { asio::write(m_client, asio::buffer(bytes)); }

    // Returns nullopt once the server has closed the connection.
    std::optional<std::string> receive() {
        FrameHeader header{};
        std::error_code ec;
        asio::read(m_client, asio::buffer(header), ec);
        if (ec) {
            return std::nullopt;
        }
        std::string payload(decodeFrameHeader(header), '\0');
        asio::read(m_client, asio::buffer(payload), ec);
        if (ec) {
            return std::nullopt;
        }
        return payload;
    }

    void closeClient() { m_client.close(); }

    std::vector<std::string> handledPayloads() {
        const std::scoped_lock lock(m_mutex);
        return m_handled;
    }

    // Polls for up to 2 s, far less than the default idle timeout.
    bool waitUntilSessionFreed() const {
        for (int i = 0; i < 2000 && !m_session.expired(); ++i) {
            std::this_thread::sleep_for(1ms);
        }
        return m_session.expired();
    }

private:
    // Runs on the I/O thread, so m_handled is guarded by m_mutex.
    std::optional<std::string> handle(std::string_view payload) {
        {
            const std::scoped_lock lock(m_mutex);
            m_handled.emplace_back(payload);
        }
        if (payload == "throw") {
            throw std::runtime_error("handler failed");
        }
        if (payload == "silent") {
            return std::nullopt;
        }
        return "echo:" + std::string(payload);
    }

    asio::io_context m_io;
    asio::io_context m_clientIo;
    asio::ip::tcp::socket m_client{m_clientIo};
    std::weak_ptr<Session> m_session;
    std::mutex m_mutex;
    std::vector<std::string> m_handled;
    std::jthread m_ioThread;
};

TEST_F(SessionTest, Frame_HandlerReturnsReply_ReplyIsFramedBack) {
    startSession();
    send("hello");
    EXPECT_EQ(receive(), "echo:hello");
}

TEST_F(SessionTest, Frames_SeveralOnOneConnection_RepliesComeBackInOrder) {
    startSession();
    // Sent back to back without waiting (pipelining).
    send("1");
    send("2");
    send("3");

    EXPECT_EQ(receive(), "echo:1");
    EXPECT_EQ(receive(), "echo:2");
    EXPECT_EQ(receive(), "echo:3");
}

TEST_F(SessionTest, Frame_HandlerReturnsNullopt_NoReplyAndNextFrameIsHandled) {
    startSession();
    send("silent");
    send("next");

    // If "silent" had been answered, this would read that reply instead.
    EXPECT_EQ(receive(), "echo:next");
    EXPECT_EQ(handledPayloads(), (std::vector<std::string>{"silent", "next"}));
}

TEST_F(SessionTest, Frame_ZeroLengthPayload_IsPassedToHandler) {
    startSession();
    send("");
    EXPECT_EQ(receive(), "echo:");
}

TEST_F(SessionTest, FrameHeader_OverMaxSize_ClosesWithoutCallingHandler) {
    startSession();
    const FrameHeader header = encodeFrameHeader(kMaxPayloadSize + 1);
    sendBytes({reinterpret_cast<const char*>(header.data()), header.size()});

    EXPECT_EQ(receive(), std::nullopt);
    EXPECT_TRUE(handledPayloads().empty());
}

TEST_F(SessionTest, Frame_HandlerThrows_ClosesConnection) {
    startSession();
    send("throw");
    EXPECT_EQ(receive(), std::nullopt);
}

TEST_F(SessionTest, IdleTimeout_PeerSendsNothing_ClosesConnectionAndFreesSession) {
    startSession(50ms);
    EXPECT_EQ(receive(), std::nullopt);
    EXPECT_TRUE(waitUntilSessionFreed());
}

TEST_F(SessionTest, IdleTimeout_PeerStallsMidFrame_ClosesConnection) {
    startSession(50ms);
    // Half a frame header. Explicit length: the bytes are NULs.
    sendBytes(std::string_view{"\x00\x00", 2});
    EXPECT_EQ(receive(), std::nullopt);
}

// Each frame moves the deadline, so the single timer wait must re-wait rather
// than close when it fires (ADR-012).
TEST_F(SessionTest, IdleTimeout_FramesKeepArriving_ConnectionOutlivesTimeout) {
    startSession(200ms);
    for (int i = 0; i < 10; ++i) {  // 400 ms in total, twice the timeout.
        std::this_thread::sleep_for(40ms);
        send("silent");
    }
    send("still-open");
    EXPECT_EQ(receive(), "echo:still-open");
}

// close() must cancel the idle timer. Otherwise its pending wait keeps the
// Session, and so its buffers, alive until the deadline (30 s here).
TEST_F(SessionTest, PeerCloses_SessionIsFreedWithoutWaitingForIdleTimeout) {
    startSession(30s);
    send("hello");
    ASSERT_EQ(receive(), "echo:hello");

    closeClient();

    EXPECT_TRUE(waitUntilSessionFreed());
}

}  // namespace
}  // namespace acs::net
