#ifndef DSO_CALIBRATION_SESSION_HPP
#define DSO_CALIBRATION_SESSION_HPP

#include "dso_calibration.hpp"

namespace dso {

enum class CalibrationSessionState : std::uint8_t {
    Idle, FirstReference, SecondReference, ValidationReference, Verified,
    Installed, Aborted, Failed
};
enum class CalibrationSessionStatus : std::uint8_t {
    None, Disabled, Physical, InvalidPoint, Fit, Bank, State
};

// Foreground owner of an ephemeral three-reference DC calibration transaction.
// The controller alone supplies fully processed, held and unrecalled records;
// this object validates summaries, not the applied reference's authenticity.
// An accepted third point exposes no live-bank change: install is separate.
// All buffers/objects are caller-owned and no peripheral/storage is accessed.
class CalibrationSession final {
public:
    CalibrationSession() noexcept = default;
    CalibrationSession(const CalibrationSession&) = delete;
    CalibrationSession& operator=(const CalibrationSession&) = delete;
    [[nodiscard]] bool begin(const PhysicalSettings& physical, CalibrationBank& bank) noexcept;
    // Invalid numerical summaries do not consume a reference. Fit/validation
    // failure ends the transaction; a fresh begin is required. Tolerance is
    // frozen only for the third point and cannot relabel a verified fit later.
    [[nodiscard]] bool accept(const CalibrationPoint& point, std::uint32_t tolerance_microvolts) noexcept;
    [[nodiscard]] bool install(const PhysicalSettings& physical, CalibrationBank& bank) noexcept;
    void abort() noexcept;
    // Any invalidity, acceptance event or mismatch aborts an active transaction
    // or clears its Installed display state, including requalification of the
    // same nominal physical switches. Installed bank profiles are untouched.
    void physical_update(const PanelUpdate& update) noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] CalibrationSessionState state() const noexcept { return state_; }
    [[nodiscard]] CalibrationSessionStatus status() const noexcept { return status_; }
    [[nodiscard]] CalibrationStatus fit_status() const noexcept { return fit_.status(); }
    [[nodiscard]] std::uint32_t validation_error_microvolts() const noexcept { return fit_.validation_error_microvolts(); }
    [[nodiscard]] std::uint32_t validation_tolerance_microvolts() const noexcept { return validation_tolerance_; }
    [[nodiscard]] const PhysicalSettings& physical() const noexcept { return first_.physical; }
private:
    CalibrationPoint first_{};
    CalibrationFit fit_{};
    std::uint64_t board_identity_{};
    std::uint32_t validation_tolerance_{};
    CalibrationSessionState state_{CalibrationSessionState::Idle};
    CalibrationSessionStatus status_{CalibrationSessionStatus::None};
};
static_assert(sizeof(CalibrationSession) <= 128U, "Revisit calibration workflow SRAM budget");

} // namespace dso
#endif
