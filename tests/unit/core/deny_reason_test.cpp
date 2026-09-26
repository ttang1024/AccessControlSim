#include "core/deny_reason.h"

#include <gtest/gtest.h>

namespace acs::core {
namespace {

TEST(DenyReasonTest, ToString_EachReason_ReturnsWireName) {
    EXPECT_EQ(toString(DenyReason::UnknownCard), "UnknownCard");
    EXPECT_EQ(toString(DenyReason::CardholderSuspended), "CardholderSuspended");
    EXPECT_EQ(toString(DenyReason::CardholderExpired), "CardholderExpired");
    EXPECT_EQ(toString(DenyReason::NoZoneAccess), "NoZoneAccess");
    EXPECT_EQ(toString(DenyReason::OutsideSchedule), "OutsideSchedule");
    EXPECT_EQ(toString(DenyReason::MalformedRequest), "MalformedRequest");
    EXPECT_EQ(toString(DenyReason::InternalError), "InternalError");
}

TEST(DenyReasonTest, DenyReasonFromString_EveryReason_RoundTrips) {
    for (const DenyReason reason : kAllDenyReasons) {
        EXPECT_EQ(denyReasonFromString(toString(reason)), reason) << toString(reason);
    }
}

TEST(DenyReasonTest, DenyReasonFromString_UnknownOrWrongCase_ReturnsNullopt) {
    EXPECT_EQ(denyReasonFromString("NoSuchReason"), std::nullopt);
    EXPECT_EQ(denyReasonFromString("unknowncard"), std::nullopt);
    EXPECT_EQ(denyReasonFromString(""), std::nullopt);
}

}  // namespace
}  // namespace acs::core
