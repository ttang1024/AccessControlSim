#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

// Like gtest_main, but silences the global logger. Tests check behaviour
// through return values and events, not log lines.
int main(int argc, char** argv) {
    spdlog::set_level(spdlog::level::off);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
