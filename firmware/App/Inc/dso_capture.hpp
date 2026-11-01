#ifndef DSO_CAPTURE_HPP
#define DSO_CAPTURE_HPP

#include <cstddef>
#include <cstdint>
#include "dso_trigger.hpp"

namespace dso {

inline constexpr std::uint16_t CaptureCapacity = 1024U;
inline constexpr std::uint16_t AdcMax = 4095U;

enum class CaptureState { Stopped, Armed, Posttrigger, Ready, Fault };
enum class TriggerMode { Normal, Auto, Single };
enum class CaptureFault { None, SequenceGap, InvalidSample };

struct CaptureConfig final {
    std::uint16_t length;       // 1..1024, including the trigger sample.
    std::uint16_t pretrigger;   // 0..length-1; crossing is at this index.
    std::uint16_t level;        // Raw ADC codes; no board gain assumption.
    std::uint16_t hysteresis;   // Rearming distance from level.
    TriggerMode mode;
    TriggerEdge edge;
    std::uint32_t auto_timeout_samples; // >=pretrigger+1 in Auto; otherwise unused.
};

// Foreground-owned static storage. ISRs/DMA never access this object. A record
// stays immutable in Ready until explicit arm/stop. Single and Normal both wait
// indefinitely; application policy decides when Normal/Auto can rearm. Single
// requires a user action. No allocation, exceptions, virtual calls or RTTI.
class Capture final {
public:
    Capture() = default;
    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;
    Capture(Capture&&) = delete;
    Capture& operator=(Capture&&) = delete;

    // Invalid configuration leaves the object untouched.
    [[nodiscard]] bool arm(const CaptureConfig& config,
                           std::uint32_t first_sequence) noexcept;
    void stop() noexcept;
    // Loss/uncertain transport invalidates an in-progress record. A previously
    // frozen Ready record remains valid. Faults require explicit rearm.
    void gap() noexcept;
    // Input: immutable non-aliasing ADC snapshot, codes 0..4095. Sequence is a
    // modulo-2^32 SAMPLE counter. Return count consumed, stopping exactly at
    // Ready/Fault. Caller handles the unconsumed tail and acquisition dead time.
    [[nodiscard]] std::size_t push(const std::uint16_t* samples, std::size_t count,
                                   std::uint32_t first_sequence) noexcept;
    [[nodiscard]] bool read(std::uint16_t index, std::uint16_t* sample) const noexcept;

    [[nodiscard]] CaptureState state() const noexcept { return state_; }
    [[nodiscard]] CaptureFault fault() const noexcept { return fault_; }
    [[nodiscard]] const CaptureConfig& config() const noexcept { return config_; }
    [[nodiscard]] std::uint32_t next_sequence() const noexcept { return next_sequence_; }
    [[nodiscard]] std::uint32_t trigger_sequence() const noexcept { return trigger_sequence_; }
    [[nodiscard]] bool edge_triggered() const noexcept { return edge_triggered_; }

private:
    [[nodiscard]] bool active() const noexcept;
    // Keep hot control fields at small offsets for Cortex-M3 Thumb loads.
    // The private sample array follows them; no serialized layout is changed.
    CaptureConfig config_{};
    CaptureState state_{CaptureState::Stopped};
    CaptureFault fault_{CaptureFault::None};
    std::uint32_t next_sequence_{};
    std::uint32_t trigger_sequence_{};
    std::uint32_t waited_{};
    std::uint16_t write_index_{};
    std::uint16_t history_{};
    std::uint16_t remaining_{};
    ThresholdCrossing crossing_{};
    bool edge_triggered_{};
    std::uint16_t samples_[CaptureCapacity]{};
};

} // namespace dso
#endif
