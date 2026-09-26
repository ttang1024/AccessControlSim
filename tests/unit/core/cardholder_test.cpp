#include "core/cardholder.h"

#include <gtest/gtest.h>

namespace acs::core {
namespace {

TEST(CardholderStatusTest, ToString_EachStatus_ReturnsLowercaseName) {
    EXPECT_EQ(toString(CardholderStatus::Active), "active");
    EXPECT_EQ(toString(CardholderStatus::Suspended), "suspended");
    EXPECT_EQ(toString(CardholderStatus::Expired), "expired");
}

TEST(CardholderStatusTest, CardholderStatusFromString_EveryStatus_RoundTrips) {
    for (const auto status :
         {CardholderStatus::Active, CardholderStatus::Suspended, CardholderStatus::Expired}) {
        EXPECT_EQ(cardholderStatusFromString(toString(status)), status);
    }
}

TEST(CardholderStatusTest, CardholderStatusFromString_UnknownOrWrongCase_ReturnsNullopt) {
    EXPECT_EQ(cardholderStatusFromString("Active"), std::nullopt);
    EXPECT_EQ(cardholderStatusFromString("deleted"), std::nullopt);
    EXPECT_EQ(cardholderStatusFromString(""), std::nullopt);
}

}  // namespace
}  // namespace acs::core
