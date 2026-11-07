#ifndef DSO_CAPTURE_CONTRACT_HPP
#define DSO_CAPTURE_CONTRACT_HPP

#include <cstdint>

namespace dso::capture_contract {

// Fixed record capacity shared by acquisition, display and host export.
inline constexpr std::uint16_t MaximumSamples{1024U};

} // namespace dso::capture_contract

#endif // DSO_CAPTURE_CONTRACT_HPP
