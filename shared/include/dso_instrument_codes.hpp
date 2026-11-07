#ifndef DSO_INSTRUMENT_CODES_HPP
#define DSO_INSTRUMENT_CODES_HPP

#include <cstdint>

namespace dso::instrument_code_contract {

// Stable byte values used in USB settings, capture metadata and Studio records.
enum class TriggerMode : std::uint8_t { Normal = 0U, Auto = 1U, Single = 2U };
enum class TriggerEdge : std::uint8_t { Rising = 0U, Falling = 1U };
enum class InputCoupling : std::uint8_t { Unknown = 0U, Dc = 1U, Ac = 2U, Ground = 3U };
enum class CalibrationKind : std::uint8_t { Unavailable = 0U, Nominal = 1U, Measured = 2U };
enum class UiState : std::uint8_t {
    Hold = 0U,
    WaitingForControls = 1U,
    WaitingForBackend = 2U,
    ArmPending = 3U,
    Acquiring = 4U,
    FrameReady = 5U,
    Fault = 6U
};
enum class HostCaptureState : std::uint8_t {
    None = 0U,
    Pending = 1U,
    Complete = 2U,
    Cancelled = 3U,
    Fault = 4U
};

static_assert(static_cast<std::uint8_t>(HostCaptureState::None) == 0U &&
                  static_cast<std::uint8_t>(HostCaptureState::Pending) == 1U &&
                  static_cast<std::uint8_t>(HostCaptureState::Complete) == 2U &&
                  static_cast<std::uint8_t>(HostCaptureState::Cancelled) == 3U &&
                  static_cast<std::uint8_t>(HostCaptureState::Fault) == 4U,
              "Host capture state bytes are fixed by USB protocol v1");

namespace capture_flags {
inline constexpr std::uint8_t EdgeTriggered{0x08U};
inline constexpr std::uint8_t ReservedMask{0xf7U};

static_assert((EdgeTriggered & ReservedMask) == 0U &&
                  (EdgeTriggered | ReservedMask) == 0xffU,
              "Only the edge-triggered capture metadata flag is defined");
} // namespace capture_flags

} // namespace dso::instrument_code_contract

#endif // DSO_INSTRUMENT_CODES_HPP
