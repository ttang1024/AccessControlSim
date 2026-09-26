#include <gtest/gtest.h>

#include <asio.hpp>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "events/audit_log_writer.h"
#include "events/event_queue.h"
#include "net/access_request_handler.h"
#include "net/frame_codec.h"
#include "net/message_codec.h"
#include "net/reader_watchdog.h"
#include "net/server.h"
#include "sim/virtual_reader.h"
#include "storage/database.h"
#include "storage/schema.h"
#include "storage/sqlite_event_repository.h"
#include "unit/manual_clock.h"
#include "unit/test_time.h"

namespace acs {
namespace {

using namespace std::chrono_literals;
using core::DenyReason;
using engine::Decision;

const core::CardCredential kKnownCard{.number = "100", .facilityCode = 42};
const core::CardCredential kUnknownCard{.number = "999", .facilityCode = 42};

// R-1 guards the Lobby and R-2 the Server Room. Card 100 may use the Lobby
// Mon-Fri 07-18. The server's clock is fixed at Monday 09:00.
engine::AccessSnapshot makeSnapshot() {
    engine::AccessSnapshot snapshot;
    snapshot.readers["R-1"] = {.id = "R-1", .zoneId = "Z-LOBBY"};
    snapshot.readers["R-2"] = {.id = "R-2", .zoneId = "Z-SERVER"};
    snapshot.schedules["S"] = {.id = "S", .name = "Weekdays", .windows = {}};
    for (unsigned day = 1; day <= 5; ++day) {
        snapshot.schedules["S"].windows.push_back({std::chrono::weekday{day}, 7h, 18h});
    }
    snapshot.accessGroups["G"] = {
        .id = "G", .name = "Staff", .zones = {"Z-LOBBY"}, .scheduleId = "S"};
    snapshot.cardholders["CH"] = {.id = "CH",
                                  .name = "Alice",
                                  .status = core::CardholderStatus::Active,
                                  .expiresAt = std::nullopt,
                                  .accessGroups = {"G"}};
    snapshot.cards[kKnownCard] = {.credential = kKnownCard, .cardholderId = "CH"};
    return snapshot;
}

storage::Database makeDatabase() {
    storage::Database db(":memory:");
    storage::applySchema(db);
    return db;
}

// A real server on an ephemeral port, wired like main(): a pool of I/O
// threads, the full audit pipeline writing to an in-memory SQLite database,
// and the heartbeat watchdog. The clock starts at Monday 09:00 and only moves
// when a test advances it.
// Shutdown order comes from member order: stop the io_context, join the threads,
// destroy the watchdog, Server and io_context, then the handler, then the writer
// (which drains the queue), then the queue and database.
class TestServer {
public:
    explicit TestServer(std::chrono::milliseconds idleTimeout = 30s, unsigned ioThreads = 4)
        : m_handler(std::make_shared<const net::AccessRequestHandler>(
              m_snapshot, m_clock, m_eventQueue, m_readerMonitor)),
          m_server(
              m_io, {asio::ip::make_address("127.0.0.1"), 0},
              [handler = m_handler](std::string_view payload) { return handler->handle(payload); },
              idleTimeout) {
        m_server.start();
        m_watchdog.start();
        for (unsigned i = 0; i < ioThreads; ++i) {
            m_threads.emplace_back([this] { m_io.run(); });
        }
    }

    TestServer(const TestServer&) = delete;
    TestServer& operator=(const TestServer&) = delete;

    ~TestServer() { m_io.stop(); }

    [[nodiscard]] std::uint16_t port() const { return m_server.port(); }

    // Shuts down in the same order as main(): stop serving, join every I/O
    // thread, then stop the audit writer (which writes out the queue). Returns
    // what was stored, newest first. The server is stopped afterwards.
    //
    // Joining the I/O threads first matters. The watchdog marks a reader
    // offline and then queues the event, so a test that sees isOnline() ==
    // false can still get here before the event is queued. Stopping the
    // writer at that point would close the queue and drop the event. That
    // was a real flaky failure on Linux, reproduced by widening the gap.
    std::vector<events::AccessEvent> flushAndLoadEvents() {
        stopServingAndFlush();
        // Safe on this thread: the writer thread has been joined.
        return m_eventRepository.loadRecent(10'000);
    }

    std::vector<events::ReaderStatusEvent> flushAndLoadReaderStatus() {
        stopServingAndFlush();
        return m_eventRepository.loadRecentReaderStatus(10'000);
    }

    void advanceClock(std::chrono::seconds by) { m_clock.advance(by); }

    // The watchdog runs on its own timer, so wait (up to 5 s) for it to notice.
    [[nodiscard]] bool waitUntilOffline(const core::ReaderId& id) const {
        for (int i = 0; i < 5000 && m_readerMonitor.isOnline(id); ++i) {
            std::this_thread::sleep_for(1ms);
        }
        return !m_readerMonitor.isOnline(id);
    }

private:
    void stopServingAndFlush() {
        m_io.stop();
        m_threads.clear();  // jthread joins. Handlers still running finish first.
        m_auditWriter.stop();
    }

    test::ManualClock m_clock{test::at(std::chrono::Monday, 9h)};
    std::shared_ptr<const engine::AccessSnapshot> m_snapshot =
        std::make_shared<const engine::AccessSnapshot>(makeSnapshot());
    storage::Database m_db{makeDatabase()};
    storage::SqliteEventRepository m_eventRepository{m_db};
    events::EventQueue m_eventQueue{10'000};
    events::ReaderMonitor m_readerMonitor{m_snapshot->readerIds(), 10s, 3, m_clock.now(),
                                          m_eventQueue};
    events::AuditLogWriter m_auditWriter{m_eventQueue, m_eventRepository};
    std::shared_ptr<const net::AccessRequestHandler> m_handler;
    asio::io_context m_io;
    net::Server m_server;
    net::ReaderWatchdog m_watchdog{m_io, m_readerMonitor, m_clock, 5ms};
    std::vector<std::jthread> m_threads;  // Declared last so these join first.
};

class ServerIntegrationTest : public ::testing::Test {
protected:
    std::unique_ptr<sim::VirtualReader> connectReader(const std::string& id = "R-1") {
        auto reader = std::make_unique<sim::VirtualReader>(id);
        const auto ec = reader->connect("127.0.0.1", m_server.port());
        EXPECT_FALSE(ec) << ec.message();
        return reader;
    }

    TestServer m_server;
};

TEST_F(ServerIntegrationTest, Swipe_KnownCardDuringSchedule_Grants) {
    EXPECT_EQ(connectReader()->swipe(kKnownCard), Decision::grant());
}

TEST_F(ServerIntegrationTest, Swipe_UnknownCard_DeniesUnknownCard) {
    EXPECT_EQ(connectReader()->swipe(kUnknownCard), Decision::deny(DenyReason::UnknownCard));
}

TEST_F(ServerIntegrationTest, Swipe_ReaderInZoneWithoutAccess_DeniesNoZoneAccess) {
    EXPECT_EQ(connectReader("R-2")->swipe(kKnownCard), Decision::deny(DenyReason::NoZoneAccess));
}

TEST_F(ServerIntegrationTest, Swipe_ManyOnOneConnection_EachAnsweredWithMatchingSeq) {
    // swipe() only returns a Decision if the response seq matches the request.
    auto reader = connectReader();
    for (int i = 0; i < 50; ++i) {
        const auto& card = i % 2 == 0 ? kKnownCard : kUnknownCard;
        ASSERT_TRUE(reader->swipe(card).has_value()) << "swipe " << i;
    }
}

TEST_F(ServerIntegrationTest, SendFrame_InvalidJson_RespondsMalformedRequestWithoutSeq) {
    auto reader = connectReader();
    ASSERT_TRUE(reader->sendFrame("this is not json"));

    const auto response = net::parseAccessResponse(reader->receiveFrame().value_or(""));

    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->seq, std::nullopt);
    EXPECT_EQ(response->decision, Decision::deny(DenyReason::MalformedRequest));
}

TEST_F(ServerIntegrationTest, SendFrame_MissingFieldsWithSeq_EchoesSeqAndKeepsConnection) {
    auto reader = connectReader();
    ASSERT_TRUE(reader->sendFrame(R"({"type":"access_request","seq":5})"));

    const auto response = net::parseAccessResponse(reader->receiveFrame().value_or(""));

    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->seq, 5U);
    EXPECT_EQ(response->decision, Decision::deny(DenyReason::MalformedRequest));
    // A bad message is not a framing error, so the connection stays usable.
    EXPECT_EQ(reader->swipe(kKnownCard), Decision::grant());
}

TEST_F(ServerIntegrationTest, SendHeartbeat_ThenSwipe_OnlySwipeIsAnswered) {
    auto reader = connectReader();
    ASSERT_TRUE(reader->sendHeartbeat());
    // If the heartbeat had a reply, swipe() would read it and fail the seq check.
    EXPECT_EQ(reader->swipe(kKnownCard), Decision::grant());
}

TEST_F(ServerIntegrationTest, FrameHeaderOverMaxSize_ServerClosesConnection) {
    auto reader = connectReader();
    const auto header = net::encodeFrameHeader(net::kMaxPayloadSize + 1);
    ASSERT_TRUE(reader->sendBytes({reinterpret_cast<const char*>(header.data()), header.size()}));

    EXPECT_EQ(reader->receiveFrame(), std::nullopt);
}

TEST_F(ServerIntegrationTest, FrameOfExactlyMaxSize_IsAccepted) {
    const std::string prefix =
        R"({"type":"access_request","reader_id":"R-1","card":"100","facility":42,"seq":1,"pad":")";
    const std::string suffix = R"("})";
    const std::string payload =
        prefix + std::string(net::kMaxPayloadSize - prefix.size() - suffix.size(), 'x') + suffix;
    ASSERT_EQ(payload.size(), net::kMaxPayloadSize);

    auto reader = connectReader();
    ASSERT_TRUE(reader->sendFrame(payload));
    const auto response = net::parseAccessResponse(reader->receiveFrame().value_or(""));

    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->decision, Decision::grant());
}

TEST_F(ServerIntegrationTest, ReaderDisconnects_ServerKeepsServingOthers) {
    connectReader().reset();  // Connect, then drop the connection straight away.
    EXPECT_EQ(connectReader()->swipe(kKnownCard), Decision::grant());
}

TEST_F(ServerIntegrationTest, Swipes_AreWrittenToAuditLog) {
    auto reader = connectReader();
    ASSERT_EQ(reader->swipe(kKnownCard), Decision::grant());
    ASSERT_EQ(reader->swipe(kUnknownCard), Decision::deny(DenyReason::UnknownCard));
    ASSERT_TRUE(reader->sendFrame("garbage"));
    ASSERT_TRUE(reader->receiveFrame().has_value());

    const auto events = m_server.flushAndLoadEvents();

    ASSERT_EQ(events.size(), 3U);  // Newest first.
    EXPECT_EQ(events[0].decision, Decision::deny(DenyReason::MalformedRequest));
    EXPECT_EQ(events[1].card, kUnknownCard);
    EXPECT_EQ(events[1].decision, Decision::deny(DenyReason::UnknownCard));
    EXPECT_EQ(events[2].readerId, "R-1");
    EXPECT_EQ(events[2].card, kKnownCard);
    EXPECT_EQ(events[2].decision, Decision::grant());
    EXPECT_EQ(events[2].timestamp, test::at(std::chrono::Monday, 9h));
}

TEST_F(ServerIntegrationTest, ReaderStopsHeartbeating_GoesOfflineThenOnlineAgain) {
    auto reader = connectReader("R-1");

    m_server.advanceClock(31s);  // More than 3 missed 10-second heartbeats.
    ASSERT_TRUE(m_server.waitUntilOffline("R-1"));

    ASSERT_TRUE(reader->sendHeartbeat());
    // A swipe on the same connection is handled after the heartbeat, so once
    // it's answered the heartbeat has been processed.
    ASSERT_TRUE(reader->swipe(kKnownCard).has_value());

    std::vector<events::ReaderStatusEvent> r1;
    for (const auto& event : m_server.flushAndLoadReaderStatus()) {
        if (event.readerId == "R-1") {
            r1.push_back(event);
        }
    }
    ASSERT_EQ(r1.size(), 2U);  // Newest first.
    EXPECT_EQ(r1[0].status, events::ReaderStatus::Online);
    EXPECT_EQ(r1[1].status, events::ReaderStatus::Offline);
}

TEST_F(ServerIntegrationTest, HeartbeatingReader_StaysOnlineWhileSilentOneGoesOffline) {
    auto r1 = connectReader("R-1");
    for (int i = 0; i < 4; ++i) {  // 40 s, heartbeating every 10 s.
        m_server.advanceClock(10s);
        ASSERT_TRUE(r1->sendHeartbeat());
        ASSERT_TRUE(r1->swipe(kKnownCard).has_value());  // Heartbeat processed.
    }
    ASSERT_TRUE(m_server.waitUntilOffline("R-2"));

    const auto statusEvents = m_server.flushAndLoadReaderStatus();
    ASSERT_EQ(statusEvents.size(), 1U);
    EXPECT_EQ(statusEvents[0].readerId, "R-2");
}

// A peer that stops mid-frame (as if it lost power) is disconnected. The
// idle timeout is real time, since it guards real sockets.
TEST(ServerIdleTimeoutTest, PeerStallsMidFrame_ServerClosesConnection) {
    TestServer server(100ms);
    sim::VirtualReader reader("R-1");
    ASSERT_FALSE(reader.connect("127.0.0.1", server.port()));

    // Half a frame header, then silence. Explicit length: the bytes are NULs.
    ASSERT_TRUE(reader.sendBytes(std::string_view{"\x00\x00", 2}));

    // Blocks until the server closes the connection; the test's TIMEOUT
    // catches the case where it never does.
    EXPECT_EQ(reader.receiveFrame(), std::nullopt);
}

// Each frame restarts the deadline, so a reader that keeps talking stays
// connected well past a single timeout period.
TEST(ServerIdleTimeoutTest, PeerSendsFramesWithinTimeout_ConnectionStaysOpen) {
    TestServer server(300ms);
    sim::VirtualReader reader("R-1");
    ASSERT_FALSE(reader.connect("127.0.0.1", server.port()));

    for (int i = 0; i < 10; ++i) {  // 500 ms in total, over the 300 ms timeout.
        std::this_thread::sleep_for(50ms);
        ASSERT_TRUE(reader.sendHeartbeat());
    }

    EXPECT_EQ(reader.swipe(kKnownCard), Decision::grant());
}

// Many readers swipe at once over separate connections. Run it under
// ThreadSanitizer (-DACS_ENABLE_TSAN=ON) to check the I/O threads for data races.
TEST_F(ServerIntegrationTest, ManyReadersSwipingConcurrently_AllGetCorrectDecisions) {
    constexpr int kReaders = 32;
    constexpr int kSwipesPerReader = 200;

    struct Result {
        int grants = 0;
        int unknownCardDenies = 0;
        int failures = 0;
    };
    std::vector<Result> results(kReaders);
    {
        std::vector<std::jthread> readers;
        for (int r = 0; r < kReaders; ++r) {
            readers.emplace_back([&, r] {
                Result& result = results[static_cast<std::size_t>(r)];
                sim::VirtualReader reader("R-1");
                if (reader.connect("127.0.0.1", m_server.port())) {
                    result.failures = kSwipesPerReader;
                    return;
                }
                for (int i = 0; i < kSwipesPerReader; ++i) {
                    const bool known = (r + i) % 2 == 0;
                    const auto decision = reader.swipe(known ? kKnownCard : kUnknownCard);
                    if (known && decision == Decision::grant()) {
                        ++result.grants;
                    } else if (!known && decision == Decision::deny(DenyReason::UnknownCard)) {
                        ++result.unknownCardDenies;
                    } else {
                        ++result.failures;
                    }
                }
            });
        }
    }  // All reader threads join here.

    for (const Result& result : results) {
        EXPECT_EQ(result.failures, 0);
        EXPECT_EQ(result.grants, kSwipesPerReader / 2);
        EXPECT_EQ(result.unknownCardDenies, kSwipesPerReader / 2);
    }
    // Every decision reached the audit log, and none were dropped.
    EXPECT_EQ(m_server.flushAndLoadEvents().size(),
              static_cast<std::size_t>(kReaders * kSwipesPerReader));
}

}  // namespace
}  // namespace acs
