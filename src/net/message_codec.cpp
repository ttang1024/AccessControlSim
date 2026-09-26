#include "net/message_codec.h"

#include <limits>
#include <nlohmann/json.hpp>

namespace acs::net {

namespace {

using nlohmann::json;

// Parses without exceptions. Returns a discarded value for invalid JSON.
json parseJson(std::string_view payload) {
    return json::parse(payload.begin(), payload.end(), nullptr, /*allow_exceptions=*/false);
}

std::optional<std::string> readString(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return std::nullopt;
    }
    return it->get<std::string>();
}

// Only accepts non-negative integers, so -1, 1.5 and "17" are all rejected.
std::optional<std::uint64_t> readUnsigned(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_unsigned()) {
        return std::nullopt;
    }
    return it->get<std::uint64_t>();
}

}  // namespace

InboundMessage parseInboundMessage(std::string_view payload) {
    const json root = parseJson(payload);
    if (root.is_discarded() || !root.is_object()) {
        return MalformedMessage{};
    }

    const auto seq = readUnsigned(root, "seq");
    const auto type = readString(root, "type");

    if (type == "heartbeat") {
        auto readerId = readString(root, "reader_id");
        if (!readerId) {
            return MalformedMessage{seq};
        }
        return HeartbeatMessage{std::move(*readerId)};
    }

    if (type != "access_request") {
        return MalformedMessage{seq};
    }

    auto readerId = readString(root, "reader_id");
    auto card = readString(root, "card");
    const auto facility = readUnsigned(root, "facility");
    if (!seq || !readerId || !card || !facility ||
        *facility > std::numeric_limits<std::uint32_t>::max()) {
        return MalformedMessage{seq};
    }

    return AccessRequestMessage{
        .seq = *seq,
        .request = {.readerId = std::move(*readerId),
                    .card = {.number = std::move(*card),
                             .facilityCode = static_cast<std::uint32_t>(*facility)}},
    };
}

std::string serialize(const AccessResponseMessage& message) {
    json root{{"type", "access_response"}};
    if (message.seq) {
        root["seq"] = *message.seq;
    }
    if (message.decision.isGranted()) {
        root["decision"] = "grant";
    } else {
        root["decision"] = "deny";
        root["reason"] = std::string{core::toString(*message.decision.denyReason())};
    }
    return root.dump();
}

std::string serialize(const AccessRequestMessage& message) {
    return json{
        {"type", "access_request"},
        {"reader_id", message.request.readerId},
        {"card", message.request.card.number},
        {"facility", message.request.card.facilityCode},
        {"seq", message.seq},
    }
        .dump();
}

std::string serialize(const HeartbeatMessage& message) {
    return json{{"type", "heartbeat"}, {"reader_id", message.readerId}}.dump();
}

std::optional<AccessResponseMessage> parseAccessResponse(std::string_view payload) {
    const json root = parseJson(payload);
    if (root.is_discarded() || !root.is_object() || readString(root, "type") != "access_response") {
        return std::nullopt;
    }

    const auto seq = readUnsigned(root, "seq");
    const auto decision = readString(root, "decision");
    if (decision == "grant") {
        return AccessResponseMessage{.seq = seq, .decision = engine::Decision::grant()};
    }
    if (decision == "deny") {
        const auto reasonName = readString(root, "reason");
        const auto reason = reasonName ? core::denyReasonFromString(*reasonName) : std::nullopt;
        if (!reason) {
            return std::nullopt;
        }
        return AccessResponseMessage{.seq = seq, .decision = engine::Decision::deny(*reason)};
    }
    return std::nullopt;
}

}  // namespace acs::net
