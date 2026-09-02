#pragma once

#include <cstdint>

// Internal to src/board/samd51: the geolocation-derived UTC offset that backs
// TimeSource::localNow() on this board. Nothing above src/board/ includes this.
namespace samd51_local_time {

// Refreshes the offset on its own long schedule. Driven by TimeSource::maintain().
void maintain();

// Seconds to add to a UTC epoch for local time. Zero -- i.e. render UTC --
// until the first lookup succeeds.
int32_t offsetSeconds();

}  // namespace samd51_local_time
