#pragma once

#include <cstdint>

namespace tva::platform {

std::int64_t unix_now();
// Offset of local time from UTC at the given instant, in minutes (DST-aware).
int local_utc_offset_minutes(std::int64_t unix_seconds);

}  // namespace tva::platform
