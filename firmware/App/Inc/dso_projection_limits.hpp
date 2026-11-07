#ifndef DSO_PROJECTION_LIMITS_HPP
#define DSO_PROJECTION_LIMITS_HPP

#include "dso_display_limits.hpp"

#include <cstdint>

namespace dso {

#if defined(DSO_PROJECTION_COLUMN_CAPACITY)
inline constexpr std::uint16_t WaveformColumnCapacity{DSO_PROJECTION_COLUMN_CAPACITY};
#else
inline constexpr std::uint16_t WaveformColumnCapacity{DisplayWidth};
#endif

static_assert(WaveformColumnCapacity >= 2U && WaveformColumnCapacity <= DisplayWidth,
              "Projection capacity must fit the LCD");

} // namespace dso

#endif // DSO_PROJECTION_LIMITS_HPP
