#pragma once

#include <filesystem>
#include <stdexcept>
#include <string_view>

#include "engine/access_snapshot.h"

namespace acs::storage {

// Thrown for an invalid seed file. The message names the bad entry, e.g.
// "cardholders[2].cards[0]: missing field 'facility'".
class SeedDataError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Parses seed data (see config/seed.json for a full example):
//
// clang-format off
// {
//   "zones":         [{ "id": "Z-LOBBY", "name": "Lobby" }],
//   "readers":       [{ "id": "R-101", "zone": "Z-LOBBY" }],
//   "schedules":     [{ "id": "S-BUS", "name": "Business Hours",
//                       "windows": [{ "days": ["Mon", "Tue"], "start": "07:00", "end": "18:00" }] }],
//   "access_groups": [{ "id": "G-STAFF", "name": "Staff", "zones": ["Z-LOBBY"], "schedule": "S-BUS" }],
//   "cardholders":   [{ "id": "CH-1", "name": "Alice", "status": "active",
//                       "expires_on": "2027-01-01", "access_groups": ["G-STAFF"],
//                       "cards": [{ "number": "10001", "facility": 42 }] }]
// }
// clang-format on
//
// - Every section is optional.
// - "status" defaults to "active".
// - "expires_on" is the first day access is denied, from 00:00 UTC.
// - Times are "HH:MM". "end" may be "24:00", and end < start crosses midnight (ADR-003).
//
// Only shape and values are checked here, such as field types, times, dates
// and duplicate IDs. Cross-references (e.g. a reader's zone) are enforced by
// the database's foreign keys when the snapshot is saved.
// Throws SeedDataError.
[[nodiscard]] engine::AccessSnapshot parseSeedData(std::string_view json);

// Reads the file and parses it. Throws SeedDataError.
[[nodiscard]] engine::AccessSnapshot loadSeedFile(const std::filesystem::path& path);

}  // namespace acs::storage
