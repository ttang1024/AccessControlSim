#include "net/message_codec.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace acs::net {
namespace {

using core::DenyReason;

TEST(MessageCodecTest, ParseInboundMessage_ValidAccessRequest_ReturnsAllFields) {
    const auto message = parseInboundMessage(
        R"({"type":"access_request","reader_id":"R-101","card":"12345","facility":42,"seq":17})");

    const auto* request = std::get_if<AccessRequestMessage>(&message);
    ASSERT_NE(request, nullptr);
    EXPECT_EQ(request->seq, 17U);
    EXPECT_EQ(request->request.readerId, "R-101");
    EXPECT_EQ(request->request.card, (core::CardCredential{.number = "12345", .facilityCode = 42}));
}

TEST(MessageCodecTest, ParseInboundMessage_ValidHeartbeat_ReturnsReaderId) {
    const auto message = parseInboundMessage(R"({"type":"heartbeat","reader_id":"R-101"})");

    const auto* heartbeat = std::get_if<HeartbeatMessage>(&message);
    ASSERT_NE(heartbeat, nullptr);
    EXPECT_EQ(heartbeat->readerId, "R-101");
}

TEST(MessageCodecTest, ParseInboundMessage_UnknownExtraFields_AreIgnored) {
    const auto message = parseInboundMessage(
        R"({"type":"access_request","reader_id":"R-1","card":"1","facility":1,"seq":1,"fw":"2.1"})");
    EXPECT_TRUE(std::holds_alternative<AccessRequestMessage>(message));
}

TEST(MessageCodecTest, ParseInboundMessage_NotJsonObject_ReturnsMalformedWithoutSeq) {
    for (const std::string payload : {"", "not json", "[1,2,3]", "42", R"("text")", "{"}) {
        const auto message = parseInboundMessage(payload);
        const auto* malformed = std::get_if<MalformedMessage>(&message);
        ASSERT_NE(malformed, nullptr) << payload;
        EXPECT_EQ(malformed->seq, std::nullopt) << payload;
    }
}

TEST(MessageCodecTest, ParseInboundMessage_BadOrMissingFields_ReturnsMalformedWithSeq) {
    const std::vector<std::string> payloads{
        R"({"seq":5})",                                                         // no type
        R"({"type":"door_forced","seq":5})",                                    // unknown type
        R"({"type":"access_request","reader_id":"R-1","facility":1,"seq":5})",  // no card
        R"({"type":"access_request","card":"1","facility":1,"seq":5})",         // no reader
        R"({"type":"access_request","reader_id":"R-1","card":"1","seq":5})",    // no facility
        R"({"type":"access_request","reader_id":"R-1","card":1,"facility":1,"seq":5})",  // card not
                                                                                         // string
        R"({"type":"access_request","reader_id":"R-1","card":"1","facility":"1","seq":5})",
        R"({"type":"access_request","reader_id":"R-1","card":"1","facility":-1,"seq":5})",
        R"({"type":"access_request","reader_id":"R-1","card":"1","facility":1.5,"seq":5})",
        R"({"type":"access_request","reader_id":"R-1","card":"1","facility":4294967296,"seq":5})",
    };
    for (const auto& payload : payloads) {
        const auto message = parseInboundMessage(payload);
        const auto* malformed = std::get_if<MalformedMessage>(&message);
        ASSERT_NE(malformed, nullptr) << payload;
        EXPECT_EQ(malformed->seq, 5U) << payload;
    }
}

TEST(MessageCodecTest, ParseInboundMessage_AccessRequestWithoutSeq_ReturnsMalformed) {
    const auto message = parseInboundMessage(
        R"({"type":"access_request","reader_id":"R-1","card":"1","facility":1})");
    EXPECT_TRUE(std::holds_alternative<MalformedMessage>(message));
}

TEST(MessageCodecTest, ParseInboundMessage_NegativeSeq_IsTreatedAsMissing) {
    const auto message = parseInboundMessage(R"({"type":"bogus","seq":-3})");
    const auto* malformed = std::get_if<MalformedMessage>(&message);
    ASSERT_NE(malformed, nullptr);
    EXPECT_EQ(malformed->seq, std::nullopt);
}

TEST(MessageCodecTest, Serialize_GrantResponse_MatchesProtocol) {
    EXPECT_EQ(serialize(AccessResponseMessage{.seq = 17, .decision = engine::Decision::grant()}),
              R"({"decision":"grant","seq":17,"type":"access_response"})");
}

TEST(MessageCodecTest, Serialize_DenyResponse_IncludesReason) {
    EXPECT_EQ(
        serialize(AccessResponseMessage{
            .seq = 17, .decision = engine::Decision::deny(DenyReason::OutsideSchedule)}),
        R"({"decision":"deny","reason":"OutsideSchedule","seq":17,"type":"access_response"})");
}

TEST(MessageCodecTest, Serialize_ResponseWithoutSeq_OmitsSeq) {
    EXPECT_EQ(
        serialize(AccessResponseMessage{
            .seq = std::nullopt, .decision = engine::Decision::deny(DenyReason::MalformedRequest)}),
        R"({"decision":"deny","reason":"MalformedRequest","type":"access_response"})");
}

TEST(MessageCodecTest, Serialize_AccessRequest_ParsesBackToSameRequest) {
    const AccessRequestMessage original{
        .seq = 99, .request = {.readerId = "R-7", .card = {.number = "555", .facilityCode = 3}}};

    const auto parsed = parseInboundMessage(serialize(original));

    const auto* request = std::get_if<AccessRequestMessage>(&parsed);
    ASSERT_NE(request, nullptr);
    EXPECT_EQ(request->seq, original.seq);
    EXPECT_EQ(request->request.readerId, original.request.readerId);
    EXPECT_EQ(request->request.card, original.request.card);
}

TEST(MessageCodecTest, Serialize_Heartbeat_ParsesBackToHeartbeat) {
    const auto parsed = parseInboundMessage(serialize(HeartbeatMessage{.readerId = "R-7"}));
    const auto* heartbeat = std::get_if<HeartbeatMessage>(&parsed);
    ASSERT_NE(heartbeat, nullptr);
    EXPECT_EQ(heartbeat->readerId, "R-7");
}

TEST(MessageCodecTest, ParseAccessResponse_EverySerializedDecision_RoundTrips) {
    std::vector<engine::Decision> decisions{engine::Decision::grant()};
    for (const DenyReason reason : core::kAllDenyReasons) {
        decisions.push_back(engine::Decision::deny(reason));
    }
    for (const auto& decision : decisions) {
        const auto parsed =
            parseAccessResponse(serialize(AccessResponseMessage{.seq = 3, .decision = decision}));
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(parsed->seq, 3U);
        EXPECT_EQ(parsed->decision, decision);
    }
}

TEST(MessageCodecTest, ParseAccessResponse_InvalidResponses_ReturnNullopt) {
    for (const std::string payload : {
             "garbage",
             R"({"type":"heartbeat","seq":1,"decision":"grant"})",
             R"({"type":"access_response","seq":1,"decision":"maybe"})",
             R"({"type":"access_response","seq":1,"decision":"deny"})",
             R"({"type":"access_response","seq":1,"decision":"deny","reason":"Nope"})",
         }) {
        EXPECT_EQ(parseAccessResponse(payload), std::nullopt) << payload;
    }
}

}  // namespace
}  // namespace acs::net
