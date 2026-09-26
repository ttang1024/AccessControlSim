#include "core/system_clock.h"

#include <gtest/gtest.h>

namespace acs::core {
namespace {

using namespace std::chrono_literals;

TEST(SystemClockTest, Now_Called_IsCloseToRealWallClock) {
    const SystemClock clock;
    const auto difference = clock.now() - std::chrono::system_clock::now();
    EXPECT_LT(std::chrono::abs(difference), 5s);
}

}  // namespace
}  // namespace acs::core
