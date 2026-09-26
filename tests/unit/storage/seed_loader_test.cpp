#include "storage/seed_loader.h"

#include <gtest/gtest.h>

#include <string>

#include "storage/schema.h"
#include "storage/sqlite_access_data_repository.h"

namespace acs::storage {
namespace {

using namespace std::chrono_literals;

// Checks that parsing fails and the error mentions `expected`.
void expectSeedError(const std::string& json, const std::string& expected) {
    try {
        (void)parseSeedData(json);
        ADD_FAILURE() << "expected SeedDataError containing '" << expected << "'";
    } catch (const SeedDataError& e) {
        EXPECT_NE(std::string(e.what()).find(expected), std::string::npos)
            << "actual message: " << e.what();
    }
}

TEST(SeedLoaderTest, ParseSeedData_EmptyObject_ReturnsEmptySnapshot) {
    EXPECT_EQ(parseSeedData("{}"), engine::AccessSnapshot{});
}

TEST(SeedLoaderTest, ParseSeedData_FullEntry_FillsEveryField) {
    const auto snapshot = parseSeedData(R"({
      "zones": [{ "id": "Z-1", "name": "Lobby" }],
      "readers": [{ "id": "R-1", "zone": "Z-1" }],
      "schedules": [{ "id": "S-1", "name": "Nights",
                      "windows": [{ "days": ["Fri", "Sat"], "start": "22:00", "end": "06:30" }] }],
      "access_groups": [{ "id": "G-1", "name": "Staff", "zones": ["Z-1"], "schedule": "S-1" }],
      "cardholders": [{ "id": "CH-1", "name": "Alice", "status": "suspended",
                        "expires_on": "2027-03-01", "access_groups": ["G-1"],
                        "cards": [{ "number": "100", "facility": 42 }] }]
    })");

    EXPECT_EQ(snapshot.zones.at("Z-1").name, "Lobby");
    EXPECT_EQ(snapshot.readers.at("R-1").zoneId, "Z-1");
    EXPECT_EQ(snapshot.schedules.at("S-1").windows,
              (std::vector<core::TimeWindow>{{std::chrono::Friday, 22h, 6h + 30min},
                                             {std::chrono::Saturday, 22h, 6h + 30min}}));
    EXPECT_EQ(snapshot.accessGroups.at("G-1").scheduleId, "S-1");

    const auto& alice = snapshot.cardholders.at("CH-1");
    EXPECT_EQ(alice.status, core::CardholderStatus::Suspended);
    EXPECT_EQ(alice.expiresAt, core::TimePoint{std::chrono::sys_days{std::chrono::year{2027} /
                                                                     std::chrono::March / 1}});
    EXPECT_EQ(alice.accessGroups, std::vector<core::AccessGroupId>{"G-1"});

    const core::CardCredential card{.number = "100", .facilityCode = 42};
    EXPECT_EQ(snapshot.cards.at(card).cardholderId, "CH-1");
}

TEST(SeedLoaderTest, ParseSeedData_NoStatusOrExpiry_DefaultsToActiveNeverExpiring) {
    const auto snapshot = parseSeedData(R"({"cardholders": [{ "id": "CH-1", "name": "A" }]})");
    EXPECT_EQ(snapshot.cardholders.at("CH-1").status, core::CardholderStatus::Active);
    EXPECT_EQ(snapshot.cardholders.at("CH-1").expiresAt, std::nullopt);
}

TEST(SeedLoaderTest, ParseSeedData_EndOf2400_IsAllowed) {
    const auto snapshot = parseSeedData(R"({"schedules": [{ "id": "S", "name": "S",
        "windows": [{ "days": ["Mon"], "start": "00:00", "end": "24:00" }] }]})");
    EXPECT_EQ(snapshot.schedules.at("S").windows.at(0).end, 24h);
}

TEST(SeedLoaderTest, ParseSeedData_InvalidInput_ThrowsWithUsefulMessage) {
    expectSeedError("{not json", "invalid JSON");
    expectSeedError("[]", "must be a JSON object");
    expectSeedError(R"({"zones": {}})", "'zones' must be an array");
    expectSeedError(R"({"zones": [{ "name": "Lobby" }]})", "zones[0]: missing field 'id'");
    expectSeedError(R"({"zones": [{ "id": "", "name": "x" }]})", "non-empty string");
    expectSeedError(R"({"zones": [{ "id": "Z", "name": "a" }, { "id": "Z", "name": "b" }]})",
                    "zones[1]: duplicate id");
    expectSeedError(R"({"cardholders": [{ "id": "C", "name": "A", "status": "retired" }]})",
                    "invalid status 'retired'");
    expectSeedError(R"({"cardholders": [{ "id": "C", "name": "A", "expires_on": "2026-02-30" }]})",
                    "does not exist");
    expectSeedError(R"({"cardholders": [{ "id": "C", "name": "A", "expires_on": "01/02/2026" }]})",
                    "expected YYYY-MM-DD");
    expectSeedError(R"({"cardholders": [{ "id": "C", "name": "A",
                        "cards": [{ "number": "1", "facility": -1 }] }]})",
                    "cardholders[0].cards[0]: 'facility' must be an integer");
    expectSeedError(R"({"cardholders": [
                        { "id": "C1", "name": "A", "cards": [{ "number": "1", "facility": 1 }] },
                        { "id": "C2", "name": "B", "cards": [{ "number": "1", "facility": 1 }] }]})",
                    "cardholders[1].cards[0]: duplicate id");
}

TEST(SeedLoaderTest, ParseSeedData_InvalidWindows_ThrowsWithUsefulMessage) {
    const auto window = [](const std::string& days, const std::string& start,
                           const std::string& end) {
        return R"({"schedules": [{ "id": "S", "name": "S", "windows": [{ "days": )" + days +
               R"(, "start": ")" + start + R"(", "end": ")" + end + R"(" }] }]})";
    };
    expectSeedError(window(R"(["Mon"])", "25:00", "26:00"), "invalid time '25:00'");
    expectSeedError(window(R"(["Mon"])", "07:60", "08:00"), "invalid time '07:60'");
    expectSeedError(window(R"(["Mon"])", "7:00", "08:00"), "invalid time '7:00'");
    expectSeedError(window(R"(["Mon"])", "24:00", "06:00"), "start cannot be 24:00");
    expectSeedError(window(R"(["Mon"])", "09:00", "09:00"), "window would be empty");
    expectSeedError(window(R"(["Monday"])", "09:00", "10:00"), "invalid day 'Monday'");
    expectSeedError(window("[]", "09:00", "10:00"), "at least one day");
}

TEST(SeedLoaderTest, LoadSeedFile_MissingFile_Throws) {
    EXPECT_THROW((void)loadSeedFile("/nonexistent/seed.json"), SeedDataError);
}

// The seed file we ship must parse, and must satisfy every foreign key when saved.
TEST(SeedLoaderTest, LoadSeedFile_ShippedSeed_ParsesAndSavesToDatabase) {
    const auto snapshot = loadSeedFile(std::string(ACS_SOURCE_DIR) + "/config/seed.json");
    EXPECT_EQ(snapshot.readers.size(), 10U);

    Database db(":memory:");
    applySchema(db);
    SqliteAccessDataRepository repository(db);
    repository.replaceAll(snapshot);
    EXPECT_EQ(repository.loadSnapshot(), snapshot);
}

}  // namespace
}  // namespace acs::storage
