#pragma once

#include "dso_adc_contract.hpp"
#include "dso_capture_contract.hpp"
#include "dso_settings_contract.hpp"
#include "dso_timebase_contract.hpp"

#include <cstdint>

namespace dso::instrument_limits
{

inline constexpr std::uint8_t AdcResolutionBits{adc_contract::ResolutionBits};
inline constexpr std::uint16_t AdcMaximumCode{adc_contract::MaximumCode};
inline constexpr std::uint16_t CaptureSampleCapacity{capture_contract::MaximumSamples};
inline constexpr std::uint8_t ClassicTimebaseCatalogueEntries{
    timebase_contract::ClassicTimebaseCatalogueEntries};
inline constexpr std::uint16_t TriggerPositionPermilleMaximum{
    settings_contract::TriggerPositionPermilleMaximum};

} // namespace dso::instrument_limits
