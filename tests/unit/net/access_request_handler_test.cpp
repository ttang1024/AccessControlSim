#include "net/access_request_handler.h"

#include <gtest/gtest.h>

#include "net/message_codec.h"
#include "unit/manual_clock.h"
#include "unit/test_time.h"

namespace acs::net {
namespace {

using namespace std::chrono_literals;
using core::DenyReason;

// One lobby reader, and one card whose group allows the lobby Mon-Fri 07-18.
std::shared_ptr<const engine::AccessSnapshot> makeSnapshot() {
    engine::AccessSnapshot snapshot;
    snapshot.readers["R-1"] = {.id = "R-1", .zoneId = "Z-LOBBY"};
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
    const core::CardCredential card{.number = "100", .facilityCode = 42};
    snapshot.cards[card] = {.credential = card, .cardholderId = "CH"};
    return std::make_shared<const engine::AccessSnapshot>(std::move(snapshot));
}

std::string requestFor(std::string_view card, std::uint64_t seq) {
    return serialize(AccessRequestMessage{
        .seq = seq,
        .request = {.readerId = "R-1", .card = {.number = std::string{card}, .facilityCode = 42}}});
}

class AccessRequestHandlerTest : public ::testing::Test {
protected:
    // Returns the parsed response, or fails the test if there isn't one.
    AccessResponseMessage respond(std::string_view payload) const {
        const auto response = m_handler.handle(payload);
        EXPECT_TRUE(response.has_value());
        const auto parsed = parseAccessResponse(response.value_or(""));
        EXPECT_TRUE(parsed.has_value());
        return parsed.value_or(AccessResponseMessage{
            .seq = std::nullopt, .decision = engine::Decision::deny(DenyReason::InternalError)});
    }

    // Everything the handler recorded so far (the queue is never closed here,
    // so popBatch only blocks if nothing was recorded).
    std::vector<events::Event> recordedEvents() { return m_events.popBatch(100); }

    test::ManualClock m_clock{test::at(std::chrono::Monday, 9h)};
    events::EventQueue m_events{100};
    events::ReaderMonitor m_readers{{"R-1"}, 10s, 3, m_clock.now(), m_events};
    AccessRequestHandler m_handler{makeSnapshot(), m_clock, m_events, m_readers};
};

TEST_F(AccessRequestHandlerTest, Handle_KnownCardDuringSchedule_ReturnsGrantWithSeq) {
    const auto response = respond(requestFor("100", 7));
    EXPECT_EQ(response.seq, 7U);
    EXPECT_EQ(response.decision, engine::Decision::grant());
}

TEST_F(AccessRequestHandlerTest, Handle_UnknownCard_ReturnsDenyUnknownCard) {
    const auto response = respond(requestFor("999", 8));
    EXPECT_EQ(response.seq, 8U);
    EXPECT_EQ(response.decision, engine::Decision::deny(DenyReason::UnknownCard));
}

TEST_F(AccessRequestHandlerTest, Handle_InvalidJson_ReturnsDenyMalformedRequestWithoutSeq) {
    const auto response = respond("{not json");
    EXPECT_EQ(response.seq, std::nullopt);
    EXPECT_EQ(response.decision, engine::Decision::deny(DenyReason::MalformedRequest));
}

TEST_F(AccessRequestHandlerTest, Handle_Heartbeat_ReturnsNoResponse) {
    EXPECT_EQ(m_handler.handle(serialize(HeartbeatMessage{.readerId = "R-1"})), std::nullopt);
}

TEST_F(AccessRequestHandlerTest, Handle_AccessRequest_RecordsEventWithClockTime) {
    (void)m_handler.handle(requestFor("999", 1));

    const events::AccessEvent expected{.timestamp = m_clock.now(),
                                       .readerId = "R-1",
                                       .card = {.number = "999", .facilityCode = 42},
                                       .decision = engine::Decision::deny(DenyReason::UnknownCard)};
    EXPECT_EQ(recordedEvents(), std::vector<events::Event>{expected});
}

TEST_F(AccessRequestHandlerTest, Handle_Malformed_RecordsMalformedEventWithEmptyFields) {
    (void)m_handler.handle("garbage");

    const events::AccessEvent expected{
        .timestamp = m_clock.now(),
        .readerId = "",
        .card = {},
        .decision = engine::Decision::deny(DenyReason::MalformedRequest)};
    EXPECT_EQ(recordedEvents(), std::vector<events::Event>{expected});
}

TEST_F(AccessRequestHandlerTest, Handle_Heartbeat_RecordsNoEvent) {
    (void)m_handler.handle(serialize(HeartbeatMessage{.readerId = "R-1"}));
    m_events.close();  // So popBatch returns empty instead of blocking.
    EXPECT_TRUE(recordedEvents().empty());
}

TEST_F(AccessRequestHandlerTest, Handle_EventQueueFull_StillAnswersAndCountsDrop) {
    events::EventQueue full{0};
    const AccessRequestHandler handler{makeSnapshot(), m_clock, full, m_readers};

    const auto response = parseAccessResponse(handler.handle(requestFor("100", 1)).value_or(""));

    // Losing an audit event must never stop the door from getting its answer.
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->decision, engine::Decision::grant());
    EXPECT_EQ(full.droppedCount(), 1U);
}

TEST_F(AccessRequestHandlerTest, Handle_HeartbeatFromOfflineReader_RecordsOnlineEvent) {
    (void)m_readers.checkTimeouts(m_clock.now() + 31s);
    ASSERT_FALSE(m_readers.isOnline("R-1"));
    (void)recordedEvents();  // Discard the ReaderOffline event.

    (void)m_handler.handle(serialize(HeartbeatMessage{.readerId = "R-1"}));

    EXPECT_TRUE(m_readers.isOnline("R-1"));
    const events::ReaderStatusEvent expected{
        .timestamp = m_clock.now(), .readerId = "R-1", .status = events::ReaderStatus::Online};
    EXPECT_EQ(recordedEvents(), std::vector<events::Event>{expected});
}

TEST_F(AccessRequestHandlerTest, Handle_HeartbeatFromUnknownReader_IsIgnored) {
    (void)m_handler.handle(serialize(HeartbeatMessage{.readerId = "R-999"}));
    m_events.close();
    EXPECT_TRUE(recordedEvents().empty());
}

TEST(AccessRequestHandlerClockTest, Handle_ClockOutsideSchedule_DeniesOutsideSchedule) {
    const test::ManualClock saturday{test::at(std::chrono::Saturday, 9h)};
    events::EventQueue events{10};
    events::ReaderMonitor readers{{"R-1"}, 10s, 3, saturday.now(), events};
    const AccessRequestHandler handler{makeSnapshot(), saturday, events, readers};

    const auto response = parseAccessResponse(handler.handle(requestFor("100", 1)).value_or(""));

    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->decision, engine::Decision::deny(DenyReason::OutsideSchedule));
}

}  // namespace
}  // namespace acs::net
