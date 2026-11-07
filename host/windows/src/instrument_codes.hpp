#pragma once

#include "dso_instrument_codes.hpp"

#include <cstdint>

// Preserve established local names while sharing stable USB and file codes.
namespace dso::instrument_code
{
using TriggerMode = instrument_code_contract::TriggerMode;
using TriggerEdge = instrument_code_contract::TriggerEdge;
using InputCoupling = instrument_code_contract::InputCoupling;
using CalibrationKind = instrument_code_contract::CalibrationKind;
using UiState = instrument_code_contract::UiState;
using HostCaptureState = instrument_code_contract::HostCaptureState;
namespace capture_flags = instrument_code_contract::capture_flags;

template<class Enum>
constexpr std::uint8_t code(Enum value) noexcept
{
    return static_cast<std::uint8_t>(value);
}

// Shared bound check for byte fields and signed UI setter arguments.
template<class Enum>
constexpr bool code_is_in_range(int value, Enum first, Enum last) noexcept
{
    return value >= static_cast<int>(code(first)) &&
           value <= static_cast<int>(code(last));
}
} // namespace dso::instrument_code
