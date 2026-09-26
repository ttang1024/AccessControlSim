#pragma once

#include <optional>

#include "core/deny_reason.h"

namespace acs::engine {

// The engine's answer: grant, or deny with a reason. The private constructor
// makes an invalid state (for example "granted but with a deny reason")
// impossible to build.
class Decision {
public:
    [[nodiscard]] static Decision grant() { return Decision{std::nullopt}; }
    [[nodiscard]] static Decision deny(core::DenyReason reason) { return Decision{reason}; }

    [[nodiscard]] bool isGranted() const { return !m_denyReason.has_value(); }
    [[nodiscard]] std::optional<core::DenyReason> denyReason() const { return m_denyReason; }

    friend bool operator==(const Decision&, const Decision&) = default;

private:
    explicit Decision(std::optional<core::DenyReason> denyReason) : m_denyReason(denyReason) {}

    std::optional<core::DenyReason> m_denyReason;
};

}  // namespace acs::engine
