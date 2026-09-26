#pragma once

#include "storage/database.h"

namespace acs::storage {

// Creates any missing tables. Safe to run on every startup.
void applySchema(Database& db);

}  // namespace acs::storage
