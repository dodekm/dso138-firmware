#ifndef DSO_CAPTURE_CONFIG_HPP
#define DSO_CAPTURE_CONFIG_HPP

#include "dso_capture_contract.hpp"
#include "dso_trigger.hpp"

#include <cstdint>

namespace dso {

inline constexpr std::uint16_t CaptureCapacity{capture_contract::MaximumSamples};

// Frozen per-record capture parameters shared with metadata and analysis.
struct CaptureConfig final {
    std::uint16_t length;       // 1..1024, including the trigger sample.
    std::uint16_t pretrigger;   // 0..length-1; crossing is at this index.
    std::uint16_t level;        // Raw ADC codes; no board gain assumption.
    std::uint16_t hysteresis;   // Rearming distance from level.
    TriggerMode mode;
    TriggerEdge edge;
    std::uint32_t auto_timeout_samples; // >=pretrigger+1 in Auto; otherwise unused.
};

} // namespace dso

#endif // DSO_CAPTURE_CONFIG_HPP
