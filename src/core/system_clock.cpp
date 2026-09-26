#include "core/system_clock.h"

namespace acs::core {

TimePoint SystemClock::now() const { return std::chrono::system_clock::now(); }

}  // namespace acs::core
