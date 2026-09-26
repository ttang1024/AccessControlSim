#include "storage/sqlite_access_data_repository.h"

#include <gtest/gtest.h>

#include "storage/schema.h"
#include "storage/storage_error.h"
#include "unit/test_time.h"

namespace acs::storage {
namespace {

using namespace std::chrono_literals;

// Covers every table, and includes a midnight-crossing window, an expiry with
// milliseconds, and a cardholder in two groups (order must survive).
engine::AccessSnapshot makeSnapshot() {
    engine::AccessSnapshot s;
    s.zones["Z-1"] = {.id = "Z-1", .name = "Lobby"};
    s.zones["Z-2"] = {.id = "Z-2", .name = "Lab"};
    s.readers["R-1"] = {.id = "R-1", .zoneId = "Z-1"};
    s.readers["R-2"] = {.id = "R-2", .zoneId = "Z-2"};
    s.schedules["S-1"] = {
        .id = "S-1",
        .name = "Nights",
        .windows = {{std::chrono::Friday, 22h, 6h}, {std::chrono::Monday, 0h, 24h}}};
    s.accessGroups["G-2"] = {.id = "G-2", .name = "Lab", .zones = {"Z-2"}, .scheduleId = "S-1"};
    s.accessGroups["G-1"] = {
        .id = "G-1", .name = "All", .zones = {"Z-1", "Z-2"}, .scheduleId = "S-1"};
    s.cardholders["CH-1"] = {.id = "CH-1",
                             .name = "Alice",
                             .status = core::CardholderStatus::Active,
                             .expiresAt = test::at(std::chrono::Monday, 9h) + 123ms,
                             .accessGroups = {"G-2", "G-1"}};
    s.cardholders["CH-2"] = {.id = "CH-2",
                             .name = "Bob",
                             .status = core::CardholderStatus::Suspended,
                             .expiresAt = std::nullopt,
                             .accessGroups = {}};
    for (const auto& [number, holder] : {std::pair{"100", "CH-1"}, std::pair{"200", "CH-2"}}) {
        const core::CardCredential credential{.number = number, .facilityCode = 42};
        s.cards[credential] = {.credential = credential, .cardholderId = holder};
    }
    return s;
}

class SqliteAccessDataRepositoryTest : public ::testing::Test {
protected:
    SqliteAccessDataRepositoryTest() { applySchema(m_db); }

    Database m_db{":memory:"};
    SqliteAccessDataRepository m_repository{m_db};
};

TEST_F(SqliteAccessDataRepositoryTest, LoadSnapshot_EmptyDatabase_ReturnsEmptySnapshot) {
    EXPECT_EQ(m_repository.loadSnapshot(), engine::AccessSnapshot{});
}

TEST_F(SqliteAccessDataRepositoryTest, ReplaceAllThenLoad_FullSnapshot_RoundTripsExactly) {
    const auto original = makeSnapshot();
    m_repository.replaceAll(original);
    EXPECT_EQ(m_repository.loadSnapshot(), original);
}

TEST_F(SqliteAccessDataRepositoryTest, ReplaceAll_CalledTwice_SecondReplacesFirst) {
    m_repository.replaceAll(makeSnapshot());

    engine::AccessSnapshot smaller;
    smaller.zones["Z-9"] = {.id = "Z-9", .name = "Garage"};
    m_repository.replaceAll(smaller);

    EXPECT_EQ(m_repository.loadSnapshot(), smaller);
}

TEST_F(SqliteAccessDataRepositoryTest, ReplaceAll_DanglingReference_ThrowsAndKeepsOldData) {
    const auto original = makeSnapshot();
    m_repository.replaceAll(original);

    auto broken = original;
    broken.readers["R-3"] = {.id = "R-3", .zoneId = "Z-MISSING"};
    try {
        m_repository.replaceAll(broken);
        ADD_FAILURE() << "expected StorageError";
    } catch (const StorageError& e) {
        // SQLite alone doesn't say which row failed; the repository adds it.
        EXPECT_NE(std::string(e.what()).find("reader 'R-3'"), std::string::npos) << e.what();
    }

    // The transaction rolled back, so the deletes were undone too.
    EXPECT_EQ(m_repository.loadSnapshot(), original);
}

TEST_F(SqliteAccessDataRepositoryTest, ReplaceAll_CardForMissingCardholder_Throws) {
    auto broken = makeSnapshot();
    const core::CardCredential orphan{.number = "999", .facilityCode = 1};
    broken.cards[orphan] = {.credential = orphan, .cardholderId = "CH-MISSING"};
    EXPECT_THROW(m_repository.replaceAll(broken), StorageError);
}

}  // namespace
}  // namespace acs::storage
