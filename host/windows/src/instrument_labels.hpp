#pragma once

#include "instrument_codes.hpp"

namespace dso::instrument_label
{

// Shared by the session status and UI formatter without making the session
// depend on the UI library. Preserve the existing fallback for unknown codes.
constexpr const char *trigger_mode_name(std::uint8_t value) noexcept
{
    switch (value)
    {
    case instrument_code::code(instrument_code::TriggerMode::Normal): return "Normal";
    case instrument_code::code(instrument_code::TriggerMode::Auto): return "Auto";
    case instrument_code::code(instrument_code::TriggerMode::Single): return "Single";
    default: return "Single";
    }
}

} // namespace dso::instrument_label
