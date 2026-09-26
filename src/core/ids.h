#pragma once

#include <string>

namespace acs::core {

// Plain aliases document intent at call sites. A tagged strong-ID type per
// entity would also stop a ZoneId being passed where a ReaderId is expected;
// that is a cheap upgrade if mix-ups ever become a real risk.
using CardholderId = std::string;
using ZoneId = std::string;
using ReaderId = std::string;
using ScheduleId = std::string;
using AccessGroupId = std::string;

}  // namespace acs::core
