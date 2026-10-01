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

// Sent once per request, so it is written directly rather than through a json
// object: building one costs a map node per key and was ~20x slower. Every
// value is a fixed ASCII token or an integer, so nothing needs escaping. Keys
// stay in the alphabetical order nlohmann::json would produce.
std::string serialize(const AccessResponseMessage& message) {
    std::string out;
    out.reserve(128);  // The longest possible reply is about 100 bytes.
    if (message.decision.isGranted()) {
        out += R"({"decision":"grant")";
    } else {
        out += R"({"decision":"deny","reason":")";
        out += core::toString(*message.decision.denyReason());
        out += '"';
    }
    if (message.seq) {
        out += R"(,"seq":)";
        out += std::to_string(*message.seq);
    }
    out += R"(,"type":"access_response"})";
    return out;
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
