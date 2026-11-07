#ifndef DSO_ACQUISITION_HPP
#define DSO_ACQUISITION_HPP

#include "dso_capture.hpp"
#include "dso_dma_ring.hpp"
#include "dso_frame_metadata.hpp"
#include "dso_physical_settings.hpp"
#include "dso_timebase.hpp"
#include <cstdint>
#include <type_traits>

namespace dso {

struct PanelUpdate;

struct AcquisitionRequest final {
    CaptureConfig capture;
    PhysicalSettings physical;
    TimebasePlan timing;
    DmaRingConfig dma;
    std::uint32_t epoch;
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
    // The acquisition owner and its sole foreground controller coordinate this
    // storage. Spectrum can borrow the stopped 2 KiB DMA workspace while the
    // frozen Capture remains in its separate sample array. Recall also borrows
    // the DMA member, but its byte/sample staging is mutually exclusive.
    friend class ScopeController;
    struct DmaBuffer final { std::uint16_t samples[DmaRingSamples]; };
    struct SpectrumBuffer final { std::int32_t words[DmaRingSamples / 2U]; };
    static_assert(sizeof(DmaBuffer) == sizeof(SpectrumBuffer), "Workspace alternatives must share storage");
    static_assert(std::is_trivial<DmaBuffer>::value && std::is_trivial<SpectrumBuffer>::value,
                  "Workspace alternatives must remain trivial wrappers");
    union Workspace final {
        DmaBuffer dma;
        SpectrumBuffer spectrum;
        Workspace() noexcept : dma{} {}
    };
    static_assert(sizeof(Workspace) == 2048U, "Keep the FFT/DMA lease within the existing 2 KiB workspace");
    enum class WorkspaceOwner : std::uint8_t { Dma, Recall, Spectrum };
    [[nodiscard]] bool recall_staging_active() const noexcept
    {
        return workspace_owner_ == WorkspaceOwner::Recall;
    }
    // Borrowed byte views are valid only after begin_recall_staging() and
    // before end_recall_staging(); callers must hold that lease.
    [[nodiscard]] std::uint8_t* recall_staging_bytes() noexcept
    {
        return reinterpret_cast<std::uint8_t*>(workspace_.dma.samples);
    }
    [[nodiscard]] const std::uint8_t* recall_staging_bytes() const noexcept
    {
        return reinterpret_cast<const std::uint8_t*>(workspace_.dma.samples);
    }
    [[nodiscard]] bool begin_spectrum_workspace(std::int32_t*& words) noexcept;
    void end_spectrum_workspace() noexcept;
    [[nodiscard]] bool begin_recall_staging() noexcept;
    void end_recall_staging() noexcept;
    [[nodiscard]] bool quiesce() noexcept;
    void fail(AcquisitionFault reason) noexcept;
    [[nodiscard]] bool valid_port() const noexcept;
    const AcquisitionPort port_;
    DmaRingMonitor monitor_;
    Capture capture_;
    FrameMetadata metadata_{};
    AcquisitionState state_{AcquisitionState::Stopped};
    AcquisitionFault fault_{AcquisitionFault::None};
    WorkspaceOwner workspace_owner_{WorkspaceOwner::Dma};
    AcquisitionStatistics statistics_{};
    std::uint32_t tail_samples_{};
    Workspace workspace_{};
    std::uint16_t scratch_[DmaHalfSamples]{};
};

} // namespace dso
#endif
