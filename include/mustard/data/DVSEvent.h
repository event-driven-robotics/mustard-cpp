#pragma once
#include <cstdint>

namespace mustard {

/// A single DVS (Dynamic Vision Sensor) event.
struct DVSEvent {
    int64_t  t{0};          ///< timestamp in microseconds
    uint16_t x{0};          ///< column (0-based)
    uint16_t y{0};          ///< row (0-based)
    bool     polarity{false}; ///< true = ON, false = OFF
};

} // namespace mustard
