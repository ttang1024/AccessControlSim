#pragma once

#include <string>

#include "core/ids.h"

namespace acs::core {

struct Zone {
    ZoneId id;
    std::string name;

    friend bool operator==(const Zone&, const Zone&) = default;
};

}  // namespace acs::core
