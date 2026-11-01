#ifndef DSO_CALIBRATION_HPP
#define DSO_CALIBRATION_HPP

#include "dso_scaling.hpp"
#include <array>
#include <cstddef>

namespace dso {

// These are summaries of a frozen DC-reference record, not individual ADC reads.
// Caller supplies the independently known applied input voltage and provenance.
struct CalibrationPoint final {
    PhysicalSettings physical{};
    std::uint32_t acquisition_epoch{};
    std::uint32_t mean_code_q16{};
    std::int64_t reference_microvolts{};
    std::uint16_t samples{};
    std::uint16_t minimum_code{};
    std::uint16_t maximum_code{};
    std::uint16_t rail_samples{};
};
// Validates the supplied summary/units only, not the authenticity, age or
// uncertainty of the applied external reference. No output is modified.
[[nodiscard]] bool calibration_point_valid(const CalibrationPoint& point) noexcept;

enum class CalibrationStatus : std::uint8_t {
    Empty, Pending, Verified, InvalidPoint, PhysicalMismatch, StaleEpoch,
    InsufficientSpan, InvalidSlope, Unrepresentable, FitResidual, ValidationFailed
};

// Two fit points plus an independent interior third point. No Measured profile
// is exposed until validation succeeds. All points must come from distinct
// acquisitions in the same physical generation. DC coupling only: DC references
// cannot characterize the AC path or the grounded input.
class CalibrationFit final {
public:
    [[nodiscard]] bool begin(const CalibrationPoint& first, const CalibrationPoint& second) noexcept;
    [[nodiscard]] bool validate(const CalibrationPoint& third, std::uint32_t tolerance_microvolts) noexcept;
    [[nodiscard]] CalibrationStatus status() const noexcept { return status_; }
    [[nodiscard]] const VoltageCalibration* profile() const noexcept { return status_ == CalibrationStatus::Verified ? &profile_ : nullptr; }
    [[nodiscard]] const PhysicalSettings& physical() const noexcept { return physical_; }
    [[nodiscard]] std::uint32_t validation_error_microvolts() const noexcept { return validation_error_; }
private:
    PhysicalSettings physical_{};
    VoltageCalibration profile_{};
    std::array<std::uint32_t, 2U> epochs_{};
    std::array<std::uint32_t, 2U> fit_codes_{};
    std::uint32_t validation_error_{};
    CalibrationStatus status_{CalibrationStatus::Empty};
};

// DSC1 retains the original range/coupling format bound. Only the nine DC
// keys are supported; do not reserve RAM for unimplemented AC/GND profiles.
inline constexpr std::size_t CalibrationSlotCount{27U};
inline constexpr std::size_t CalibrationDcSlotCount{9U};
inline constexpr std::size_t CalibrationRecordMaximum{24U + CalibrationSlotCount * 20U};
inline constexpr std::size_t CalibrationSupportedRecordMaximum{24U + CalibrationDcSlotCount * 20U};

// Validate canonical DSC1 bytes without modifying them or a live bank. The
// expected identity must be nonzero. CRC checks corruption, not authenticity.
[[nodiscard]] bool calibration_record_valid(const std::uint8_t* input, std::size_t size,
                                            std::uint64_t expected_board) noexcept;

// RAM-only bank, bound to an explicit nonzero board/profile identity. Missing
// keys stay RAW. DC fits never populate AC/GND slots. Canonical byte export has
// schema, board identity and CRC32; import validates everything before mutation.
// Buffers overlapping this bank are rejected. Foreground-only: no concurrent
// import/install/resolve. CRC protects corruption, not provenance/authenticity.
class CalibrationBank final {
public:
    explicit CalibrationBank(std::uint64_t board_identity) noexcept : board_identity_{board_identity} {}
    CalibrationBank(const CalibrationBank&) = delete;
    CalibrationBank& operator=(const CalibrationBank&) = delete;
    [[nodiscard]] bool install(const CalibrationFit& fit) noexcept;
    [[nodiscard]] VoltageCalibration resolve(const PhysicalSettings& physical) const noexcept;
    [[nodiscard]] std::uint32_t revision(const PhysicalSettings& physical) const noexcept;
    [[nodiscard]] std::uint64_t board_identity() const noexcept { return board_identity_; }
    [[nodiscard]] std::size_t export_record(std::uint8_t* output, std::size_t capacity) const noexcept;
    [[nodiscard]] bool import_record(const std::uint8_t* input, std::size_t size) noexcept;
    void clear() noexcept;
private:
    struct Entry final { VoltageCalibration profile{}; std::uint32_t revision{}; };
    const std::uint64_t board_identity_;
    std::array<Entry, CalibrationDcSlotCount> entries_{};
    std::uint32_t next_revision_{1U};
};
static_assert(sizeof(CalibrationBank) <= 160U, "Revisit calibration SRAM budget");
static_assert(CalibrationRecordMaximum <= 1024U, "Keep bounded export scratch");

} // namespace dso
#endif
