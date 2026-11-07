#ifndef DSO_SETTINGS_CONTRACT_HPP
#define DSO_SETTINGS_CONTRACT_HPP

#include <cstdint>

namespace dso::settings_contract {

// Bound shared by the firmware and Studio trigger-setting interfaces.
inline constexpr std::uint16_t TriggerPositionPermilleMaximum{1000U};

} // namespace dso::settings_contract

#endif // DSO_SETTINGS_CONTRACT_HPP
