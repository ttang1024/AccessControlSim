#include "storage/sqlite_event_repository.h"

#include <gtest/gtest.h>

#include <vector>

#include "storage/schema.h"
#include "unit/test_time.h"

namespace acs::storage {
namespace {

using namespace std::chrono_literals;
using core::DenyReason;

events::AccessEvent makeEvent(std::chrono::milliseconds offset, engine::Decision decision) {
    return {.timestamp = test::at(std::chrono::Monday, 9h) + offset,
            .readerId = "R-1",
            .card = {.number = "100", .facilityCode = 42},
            .decision = decision};
}

class SqliteEventRepositoryTest : public ::testing::Test {
protected:
    SqliteEventRepositoryTest() { applySchema(m_db); }

    Database m_db{":memory:"};
    SqliteEventRepository m_repository{m_db};
};

TEST_F(SqliteEventRepositoryTest, AppendThenLoadRecent_GrantAndDeny_RoundTripNewestFirst) {
    const auto grant = makeEvent(1ms, engine::Decision::grant());
    const auto deny = makeEvent(2ms, engine::Decision::deny(DenyReason::OutsideSchedule));
    ASSERT_TRUE(m_repository.append(std::vector<events::Event>{grant, deny}));

    EXPECT_EQ(m_repository.loadRecent(10), (std::vector{deny, grant}));
}

TEST_F(SqliteEventRepositoryTest, LoadRecent_MoreEventsThanLimit_ReturnsNewestOnly) {
    std::vector<events::Event> events;
    for (int i = 0; i < 5; ++i) {
        events.emplace_back(makeEvent(std::chrono::milliseconds{i}, engine::Decision::grant()));
    }
    ASSERT_TRUE(m_repository.append(events));

    const auto loaded = m_repository.loadRecent(2);

    ASSERT_EQ(loaded.size(), 2U);
    EXPECT_EQ(events::Event{loaded[0]}, events[4]);
    EXPECT_EQ(events::Event{loaded[1]}, events[3]);
}

TEST_F(SqliteEventRepositoryTest, Append_MalformedRequestWithEmptyFields_IsStored) {
    const events::AccessEvent malformed{
        .timestamp = test::at(std::chrono::Monday, 9h),
        .readerId = "",
        .card = {},
        .decision = engine::Decision::deny(DenyReason::MalformedRequest)};
    ASSERT_TRUE(m_repository.append(std::vector<events::Event>{malformed}));

    EXPECT_EQ(m_repository.loadRecent(1), std::vector{malformed});
}

TEST_F(SqliteEventRepositoryTest, Append_TableMissing_ReturnsFalseInsteadOfThrowing) {
    m_db.execute("DROP TABLE access_events;");
    EXPECT_FALSE(
        m_repository.append(std::vector<events::Event>{makeEvent(0ms, engine::Decision::grant())}));
}

TEST_F(SqliteEventRepositoryTest, Append_MixedBatch_StoresEachTypeInItsTable) {
    const auto access = makeEvent(0ms, engine::Decision::grant());
    const events::ReaderStatusEvent offline{.timestamp = test::at(std::chrono::Monday, 9h) + 5ms,
                                            .readerId = "R-7",
                                            .status = events::ReaderStatus::Offline};
    const events::ReaderStatusEvent online{.timestamp = test::at(std::chrono::Monday, 9h) + 9ms,
                                           .readerId = "R-7",
                                           .status = events::ReaderStatus::Online};
    ASSERT_TRUE(m_repository.append(std::vector<events::Event>{access, offline, online}));

    EXPECT_EQ(m_repository.loadRecent(10), std::vector{access});
    EXPECT_EQ(m_repository.loadRecentReaderStatus(10), (std::vector{online, offline}));
}

}  // namespace
}  // namespace acs::storage
