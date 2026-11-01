#ifndef DSO_ACQUISITION_HPP
#define DSO_ACQUISITION_HPP

#include "dso_capture.hpp"
#include "dso_dma_ring.hpp"
#include "dso_front_panel.hpp"
#include "dso_timebase.hpp"

namespace dso {

struct AcquisitionRequest final {
    CaptureConfig capture;
    PhysicalSettings physical;
    TimebasePlan timing;
    DmaRingConfig dma;
    std::uint32_t epoch;
};
enum class SampleProcessingKind : std::uint8_t { Point, Filtered25 };
struct SampleProcessing final {
    SampleProcessingKind kind{SampleProcessingKind::Point};
    bool source_clipped{}; // A raw rail hit contributed to this filtered window.
    bool output_clamped{}; // FIR overshoot exceeded the output ADC code range.
};
struct FrameMetadata final {
    PhysicalSettings physical;
    TimebasePlan timing;
    CaptureConfig capture;
    std::uint32_t acquisition_epoch;
    std::uint32_t trigger_sequence;
    bool edge_triggered;
    SampleProcessing processing{};
};
enum class AcquisitionState { Stopped, Running, Ready, Fault };
enum class AcquisitionFault { None, Transport, InvalidSamples, PhysicalChange, PortFailure, Protocol };
enum class AcquisitionStopResult : std::uint8_t { Inactive, Stopped, Failed };
struct AcquisitionStatistics final {
    std::uint32_t epochs_started;
    std::uint32_t frames_ready;
    std::uint32_t blocks_committed;
    std::uint32_t faults;
    std::uint32_t transport_faults;
    std::uint32_t physical_change_faults;
    std::uint32_t discarded_tail_samples;
};

// Callbacks are foreground-only, bounded, must not throw or re-enter either
// owner. stop performs the complete checked shutdown, including trigger stop
// before DMA/ADC stop, even after a failed/partially completed start.
// observe brackets register access conservatively as described by DmaObservation.
// HT/TC are exclusively acknowledged by this owner. The port must preflight and
// verify the actual DMA setup rather than trusting HAL_ADC_Start_DMA's status.
// This STM32F103 backend contract uses a cycle counter at TIMxCLK: the DMA
// sample_period_cycles must equal the timer plan's total divider. A different
// counter clock requires a separately reviewed conversion, never silent reuse.
struct AcquisitionPort final {
    void* context;
    bool (*prepare)(void*, const TimebasePlan&) noexcept;
    bool (*arm_dma)(void*, std::uint16_t*, std::uint16_t) noexcept;
    bool (*start_trigger)(void*) noexcept;
    // One checked lifecycle operation; hardware adapter owns stop ordering.
    bool (*stop)(void*) noexcept;
    DmaObservation (*observe)(void*, bool before) noexcept;
    void (*acknowledge)(void*, std::uint8_t flag) noexcept;
};

// Owns DMA working storage, private scratch and the frozen capture. Rendering
// reads ONLY copied samples through read(); DMA storage is never published.
// Ready remains immutable until release(), including on physical-control changes.
// Caller must cancel/finish all frame consumers BEFORE release(). No automatic
// rearm: Normal/Auto/Single policy is explicitly a foreground application choice.
class DmaAcquisition final {
public:
    explicit DmaAcquisition(AcquisitionPort port) noexcept : port_{port} {}
    DmaAcquisition(const DmaAcquisition&) = delete;
    DmaAcquisition& operator=(const DmaAcquisition&) = delete;
    [[nodiscard]] bool start(const AcquisitionRequest& request) noexcept;
    void poll() noexcept;
    void physical_update(const PanelUpdate& update) noexcept;
    // Inactive means no Running epoch was stopped; Stopped/Failed describe a
    // Running epoch's shutdown result. Ready data remains preserved.
    AcquisitionStopResult stop() noexcept;
    [[nodiscard]] bool release() noexcept;
    [[nodiscard]] bool read(std::uint16_t index, std::uint16_t& value) const noexcept;
    [[nodiscard]] const FrameMetadata* metadata() const noexcept;
    [[nodiscard]] AcquisitionState state() const noexcept { return state_; }
    [[nodiscard]] AcquisitionFault fault() const noexcept { return fault_; }
    [[nodiscard]] DmaRingFault transport_fault() const noexcept { return monitor_.fault(); }
    [[nodiscard]] std::uint16_t record_length() const noexcept { return capture_.config().length; }
    [[nodiscard]] std::uint32_t trigger_sequence() const noexcept { return capture_.trigger_sequence(); }
    // Foreground-only one-consumer event; never call from an ISR.
    [[nodiscard]] bool take_accepted_edge() noexcept { return capture_.take_accepted_edge(); }
    [[nodiscard]] std::uint32_t discarded_tail_samples() const noexcept { return tail_samples_; }
    // Foreground-only cumulative counters, saturating rather than wrapping.
    [[nodiscard]] const AcquisitionStatistics& statistics() const noexcept { return statistics_; }
    [[nodiscard]] const DmaTimingStatistics& timing_statistics() const noexcept { return monitor_.timing_statistics(); }

private:
    // Foreground recall staging borrows the stopped DMA array. Only the
    // controller can hold this lease; no HAL callback or frame consumer sees it.
    friend class ScopeController;
    [[nodiscard]] bool quiesce() noexcept;
    void fail(AcquisitionFault reason) noexcept;
    [[nodiscard]] bool valid_port() const noexcept;
    const AcquisitionPort port_;
    DmaRingMonitor monitor_;
    Capture capture_;
    FrameMetadata metadata_{};
    AcquisitionState state_{AcquisitionState::Stopped};
    AcquisitionFault fault_{AcquisitionFault::None};
    bool recall_staging_{};
    AcquisitionStatistics statistics_{};
    std::uint32_t tail_samples_{};
    alignas(4) std::uint16_t dma_[DmaRingSamples]{};
    std::uint16_t scratch_[DmaHalfSamples]{};
};

} // namespace dso
#endif
