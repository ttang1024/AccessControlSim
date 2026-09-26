#include "core/schedule.h"

#include <gtest/gtest.h>

#include "unit/test_time.h"

namespace acs::core {
namespace {

using namespace std::chrono_literals;
using std::chrono::Friday;
using std::chrono::Monday;
using std::chrono::Saturday;
using std::chrono::Sunday;
using std::chrono::Thursday;
using test::at;

Schedule businessHours() {
    Schedule schedule{.id = "S-BUS", .name = "Business Hours", .windows = {}};
    for (unsigned day = Monday.c_encoding(); day <= Friday.c_encoding(); ++day) {
        schedule.windows.push_back({std::chrono::weekday{day}, 7h, 18h});
    }
    return schedule;
}

Schedule withWindow(TimeWindow window) {
    return Schedule{.id = "S", .name = "Test", .windows = {window}};
}

TEST(ScheduleTest, IsActiveAt_InsideWindow_ReturnsTrue) {
    EXPECT_TRUE(businessHours().isActiveAt(at(Monday, 12h)));
}

TEST(ScheduleTest, IsActiveAt_ExactlyAtWindowStart_ReturnsTrue) {
    EXPECT_TRUE(businessHours().isActiveAt(at(Monday, 7h)));
}

TEST(ScheduleTest, IsActiveAt_OneSecondBeforeWindowStart_ReturnsFalse) {
    EXPECT_FALSE(businessHours().isActiveAt(at(Monday, 6h, 59min, 59s)));
}

TEST(ScheduleTest, IsActiveAt_OneSecondBeforeWindowEnd_ReturnsTrue) {
    EXPECT_TRUE(businessHours().isActiveAt(at(Monday, 17h, 59min, 59s)));
}

TEST(ScheduleTest, IsActiveAt_ExactlyAtWindowEnd_ReturnsFalse) {
    EXPECT_FALSE(businessHours().isActiveAt(at(Monday, 18h)));
}

TEST(ScheduleTest, IsActiveAt_WeekendDayForWeekdaySchedule_ReturnsFalse) {
    EXPECT_FALSE(businessHours().isActiveAt(at(Saturday, 12h)));
    EXPECT_FALSE(businessHours().isActiveAt(at(Sunday, 12h)));
}

TEST(ScheduleTest, IsActiveAt_NoWindows_ReturnsFalse) {
    const Schedule empty{.id = "S", .name = "Never", .windows = {}};
    EXPECT_FALSE(empty.isActiveAt(at(Monday, 12h)));
}

TEST(ScheduleTest, IsActiveAt_ZeroLengthWindow_ReturnsFalse) {
    EXPECT_FALSE(withWindow({Monday, 9h, 9h}).isActiveAt(at(Monday, 9h)));
}

TEST(ScheduleTest, IsActiveAt_AllDayWindowLastSecond_ReturnsTrue) {
    EXPECT_TRUE(withWindow({Monday, 0h, 24h}).isActiveAt(at(Monday, 23h, 59min, 59s)));
}

TEST(ScheduleTest, IsActiveAt_AllDayWindowNextMidnight_ReturnsFalse) {
    EXPECT_FALSE(withWindow({Monday, 0h, 24h}).isActiveAt(at(std::chrono::Tuesday, 0h)));
}

// Night shift: Friday 22:00 up to Saturday 06:00.
class MidnightCrossoverTest : public ::testing::Test {
protected:
    Schedule m_nightShift = withWindow({Friday, 22h, 6h});
};

TEST_F(MidnightCrossoverTest, IsActiveAt_BeforeStartOnStartDay_ReturnsFalse) {
    EXPECT_FALSE(m_nightShift.isActiveAt(at(Friday, 21h, 59min)));
}

TEST_F(MidnightCrossoverTest, IsActiveAt_LateOnStartDay_ReturnsTrue) {
    EXPECT_TRUE(m_nightShift.isActiveAt(at(Friday, 23h, 30min)));
}

TEST_F(MidnightCrossoverTest, IsActiveAt_ExactlyMidnight_ReturnsTrue) {
    EXPECT_TRUE(m_nightShift.isActiveAt(at(Saturday, 0h)));
}

TEST_F(MidnightCrossoverTest, IsActiveAt_EarlyOnNextDay_ReturnsTrue) {
    EXPECT_TRUE(m_nightShift.isActiveAt(at(Saturday, 5h, 59min, 59s)));
}

TEST_F(MidnightCrossoverTest, IsActiveAt_AtEndOnNextDay_ReturnsFalse) {
    EXPECT_FALSE(m_nightShift.isActiveAt(at(Saturday, 6h)));
}

TEST_F(MidnightCrossoverTest, IsActiveAt_EarlyOnStartDay_ReturnsFalse) {
    // Friday 03:00 belongs to Thursday night's shift, which is not configured.
    EXPECT_FALSE(m_nightShift.isActiveAt(at(Friday, 3h)));
}

TEST_F(MidnightCrossoverTest, IsActiveAt_LateOnNextDay_ReturnsFalse) {
    EXPECT_FALSE(m_nightShift.isActiveAt(at(Saturday, 23h)));
}

TEST_F(MidnightCrossoverTest, IsActiveAt_LateOnPreviousDay_ReturnsFalse) {
    EXPECT_FALSE(m_nightShift.isActiveAt(at(Thursday, 23h)));
}

TEST(ScheduleTest, IsActiveAt_CrossoverFromSaturdayIntoSunday_WrapsWeek) {
    // Sunday's C encoding is 0 and Saturday's is 6, so this checks that the
    // day after Saturday wraps round to Sunday.
    const Schedule saturdayNight = withWindow({Saturday, 22h, 2h});
    EXPECT_TRUE(saturdayNight.isActiveAt(at(Sunday, 1h)));
    EXPECT_FALSE(saturdayNight.isActiveAt(at(Sunday, 2h)));
}

}  // namespace
}  // namespace acs::core
