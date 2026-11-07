#ifndef DSO_FRAME_METADATA_HPP
#define DSO_FRAME_METADATA_HPP

#include "dso_adc_codes.hpp"
#include "dso_capture_config.hpp"
#include "dso_physical_settings.hpp"
#include "dso_timebase.hpp"

#include <cstdint>

namespace dso {

// Value snapshot attached to a completed record. It depends on capture settings,
// not the capture engine or DMA owner, so analysis can use it independently.
struct FrameMetadata final {
    PhysicalSettings physical;
    TimebasePlan timing;
    CaptureConfig capture;
    std::uint32_t acquisition_epoch;
    std::uint32_t trigger_sequence;
    bool edge_triggered;
};

} // namespace dso

#endif // DSO_FRAME_METADATA_HPP
