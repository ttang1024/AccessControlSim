#include "storage/seed_loader.h"

#include <array>
#include <charconv>
#include <chrono>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

namespace acs::storage {

namespace {

using nlohmann::json;
using namespace std::chrono_literals;

[[noreturn]] void fail(const std::string& where, const std::string& what) {
    throw SeedDataError(where + ": " + what);
}

const json& field(const json& object, const std::string& where, const char* key) {
    const auto it = object.find(key);
    if (it == object.end()) {
        fail(where, std::string("missing field '") + key + "'");
    }
    return *it;
}

std::string requireString(const json& object, const std::string& where, const char* key) {
    const json& value = field(object, where, key);
    if (!value.is_string() || value.get_ref<const std::string&>().empty()) {
        fail(where, std::string("'") + key + "' must be a non-empty string");
    }
    return value.get<std::string>();
}

// A missing section counts as empty; anything that isn't an array is an error.
const json& arrayOrEmpty(const json& object, const std::string& where, const char* key) {
    static const json kEmpty = json::array();
    const auto it = object.find(key);
    if (it == object.end()) {
        return kEmpty;
    }
    if (!it->is_array()) {
        fail(where, std::string("'") + key + "' must be an array");
    }
    return *it;
}

std::vector<std::string> requireStringArray(const json& object, const std::string& where,
                                            const char* key) {
    std::vector<std::string> result;
    for (const json& item : arrayOrEmpty(object, where, key)) {
        if (!item.is_string()) {
            fail(where, std::string("'") + key + "' must contain only strings");
        }
        result.push_back(item.get<std::string>());
    }
    return result;
}

std::string indexed(const char* section, std::size_t index) {
    return std::string(section) + "[" + std::to_string(index) + "]";
}

// Parses exactly `length` digits starting at `offset`, or fails.
bool parseDigits(std::string_view text, std::size_t offset, std::size_t length, int& out) {
    const char* begin = text.data() + offset;
    const char* end = begin + length;
    const auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc{} && ptr == end;
}

// "HH:MM" -> minutes since midnight, from 0 to 24:00 inclusive.
std::chrono::minutes parseTimeOfDay(std::string_view text, const std::string& where) {
    int hours = 0;
    int minutes = 0;
    if (text.size() != 5 || text[2] != ':' || !parseDigits(text, 0, 2, hours) ||
        !parseDigits(text, 3, 2, minutes) || minutes > 59 || hours > 24 ||
        (hours == 24 && minutes != 0)) {
        fail(where, "invalid time '" + std::string(text) + "', expected HH:MM");
    }
    return std::chrono::hours{hours} + std::chrono::minutes{minutes};
}

std::chrono::weekday parseWeekday(std::string_view name, const std::string& where) {
    static constexpr std::array<std::string_view, 7> kNames{"Sun", "Mon", "Tue", "Wed",
                                                            "Thu", "Fri", "Sat"};
    for (unsigned i = 0; i < kNames.size(); ++i) {
        if (kNames[i] == name) {
            return std::chrono::weekday{i};
        }
    }
    fail(where, "invalid day '" + std::string(name) + "', expected Mon..Sun");
}

// "YYYY-MM-DD" -> 00:00 UTC on that date.
core::TimePoint parseDate(std::string_view text, const std::string& where) {
    int year = 0;
    int month = 0;
    int day = 0;
    if (text.size() != 10 || text[4] != '-' || text[7] != '-' || !parseDigits(text, 0, 4, year) ||
        !parseDigits(text, 5, 2, month) || !parseDigits(text, 8, 2, day)) {
        fail(where, "invalid date '" + std::string(text) + "', expected YYYY-MM-DD");
    }
    const std::chrono::year_month_day date{std::chrono::year{year},
                                           std::chrono::month{static_cast<unsigned>(month)},
                                           std::chrono::day{static_cast<unsigned>(day)}};
    if (!date.ok()) {
        fail(where, "date '" + std::string(text) + "' does not exist");
    }
    return std::chrono::sys_days{date};
}

// Inserts into an ID-keyed map, and treats a duplicate ID as an error instead
// of silently overwriting.
template <typename Map, typename Value>
void insertUnique(Map& map, const typename Map::key_type& key, Value value,
                  const std::string& where) {
    if (!map.emplace(key, std::move(value)).second) {
        fail(where, "duplicate id");
    }
}

// insertUnique for entities keyed by their own `id` field.
template <typename Map, typename Value>
void insertById(Map& map, Value value, const std::string& where) {
    const typename Map::key_type id = value.id;  // Copied: `value` is moved next.
    insertUnique(map, id, std::move(value), where);
}

void parseSchedule(const json& item, const std::string& where, engine::AccessSnapshot& out) {
    core::Schedule schedule{.id = requireString(item, where, "id"),
                            .name = requireString(item, where, "name"),
                            .windows = {}};
    const json& windows = arrayOrEmpty(item, where, "windows");
    for (std::size_t w = 0; w < windows.size(); ++w) {
        const std::string windowWhere = where + "." + indexed("windows", w);
        const auto start =
            parseTimeOfDay(requireString(windows[w], windowWhere, "start"), windowWhere);
        const auto end = parseTimeOfDay(requireString(windows[w], windowWhere, "end"), windowWhere);
        if (start == 24h) {
            fail(windowWhere, "start cannot be 24:00");
        }
        if (start == end) {
            fail(windowWhere, "start and end are equal, so the window would be empty");
        }
        const auto days = requireStringArray(windows[w], windowWhere, "days");
        if (days.empty()) {
            fail(windowWhere, "'days' must list at least one day");
        }
        for (const std::string& day : days) {
            schedule.windows.push_back(
                {.day = parseWeekday(day, windowWhere), .start = start, .end = end});
        }
    }
    insertById(out.schedules, std::move(schedule), where);
}

void parseCardholder(const json& item, const std::string& where, engine::AccessSnapshot& out) {
    core::Cardholder cardholder{.id = requireString(item, where, "id"),
                                .name = requireString(item, where, "name"),
                                .status = core::CardholderStatus::Active,
                                .expiresAt = std::nullopt,
                                .accessGroups = requireStringArray(item, where, "access_groups")};
    if (item.contains("status")) {
        const std::string status = requireString(item, where, "status");
        const auto parsed = core::cardholderStatusFromString(status);
        if (!parsed) {
            fail(where, "invalid status '" + status + "', expected active|suspended|expired");
        }
        cardholder.status = *parsed;
    }
    if (item.contains("expires_on")) {
        cardholder.expiresAt = parseDate(requireString(item, where, "expires_on"), where);
    }

    const json& cards = arrayOrEmpty(item, where, "cards");
    for (std::size_t c = 0; c < cards.size(); ++c) {
        const std::string cardWhere = where + "." + indexed("cards", c);
        const json& facility = field(cards[c], cardWhere, "facility");
        if (!facility.is_number_unsigned() ||
            facility.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max()) {
            fail(cardWhere, "'facility' must be an integer from 0 to 4294967295");
        }
        const core::CardCredential credential{
            .number = requireString(cards[c], cardWhere, "number"),
            .facilityCode = facility.get<std::uint32_t>()};
        insertUnique(out.cards, credential,
                     core::Card{.credential = credential, .cardholderId = cardholder.id},
                     cardWhere);
    }

    insertById(out.cardholders, std::move(cardholder), where);
}

}  // namespace

engine::AccessSnapshot parseSeedData(std::string_view text) {
    json root;
    try {
        root = json::parse(text);
    } catch (const json::parse_error& e) {
        throw SeedDataError(std::string("invalid JSON: ") + e.what());
    }
    if (!root.is_object()) {
        throw SeedDataError("seed data must be a JSON object");
    }

    engine::AccessSnapshot snapshot;

    const json& zones = arrayOrEmpty(root, "seed", "zones");
    for (std::size_t i = 0; i < zones.size(); ++i) {
        const std::string where = indexed("zones", i);
        core::Zone zone{.id = requireString(zones[i], where, "id"),
                        .name = requireString(zones[i], where, "name")};
        insertById(snapshot.zones, std::move(zone), where);
    }

    const json& readers = arrayOrEmpty(root, "seed", "readers");
    for (std::size_t i = 0; i < readers.size(); ++i) {
        const std::string where = indexed("readers", i);
        core::Reader reader{.id = requireString(readers[i], where, "id"),
                            .zoneId = requireString(readers[i], where, "zone")};
        insertById(snapshot.readers, std::move(reader), where);
    }

    const json& schedules = arrayOrEmpty(root, "seed", "schedules");
    for (std::size_t i = 0; i < schedules.size(); ++i) {
        parseSchedule(schedules[i], indexed("schedules", i), snapshot);
    }

    const json& groups = arrayOrEmpty(root, "seed", "access_groups");
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const std::string where = indexed("access_groups", i);
        const auto zoneIds = requireStringArray(groups[i], where, "zones");
        core::AccessGroup group{.id = requireString(groups[i], where, "id"),
                                .name = requireString(groups[i], where, "name"),
                                .zones = {zoneIds.begin(), zoneIds.end()},
                                .scheduleId = requireString(groups[i], where, "schedule")};
        insertById(snapshot.accessGroups, std::move(group), where);
    }

    const json& cardholders = arrayOrEmpty(root, "seed", "cardholders");
    for (std::size_t i = 0; i < cardholders.size(); ++i) {
        parseCardholder(cardholders[i], indexed("cardholders", i), snapshot);
    }

    return snapshot;
}

engine::AccessSnapshot loadSeedFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw SeedDataError("cannot open seed file '" + path.string() + "'");
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return parseSeedData(contents.str());
}

}  // namespace acs::storage
