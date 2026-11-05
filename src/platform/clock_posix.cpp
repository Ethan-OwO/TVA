#include <time.h>

#include <chrono>

#include "platform/clock.h"

namespace tva::platform {

std::int64_t unix_now() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

int local_utc_offset_minutes(std::int64_t unix_seconds) {
  ::tzset();
  time_t t = static_cast<time_t>(unix_seconds);
  struct tm local{};
  if (::localtime_r(&t, &local) == nullptr) return 0;
  return static_cast<int>(local.tm_gmtoff / 60);
}

}  // namespace tva::platform
