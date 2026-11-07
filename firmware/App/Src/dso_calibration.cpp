#include "dso_calibration.hpp"
#include "dso_adc_codes.hpp"
#include "dso_calibration_contract.hpp"
#include "dso_binary_endian.hpp"
#include "dso_capture_config.hpp"
#include "dso_crc32.hpp"
#include "dso_physical_settings.hpp"
#include "dso_u64_division.hpp"
#include "dso_voltage_math.hpp"

namespace dso {
namespace {
constexpr std::uint32_t MaximumCodeQ16{std::uint32_t{AdcMax} * 65536U};
constexpr std::size_t HeaderBytes{20U};
constexpr std::size_t EntryBytes{20U};

int slot(const PhysicalSettings& physical) noexcept
{
    if (physical.generation == 0U || physical.coupling != InputCoupling::Dc) { return -1; }
    for (unsigned range{}; range < CalibrationDcSlotCount; ++range) {
        if (PhysicalScaleCatalogueMicrovoltsPerDivision[range] == physical.microvolts_per_div) {
            return static_cast<int>(range);
        }
    }
    return -1;
}

bool valid_point(const CalibrationPoint& point) noexcept
{
    return calibration_physical_valid(point.physical) &&
        point.acquisition_epoch != 0U && point.samples >= 64U && point.samples <= CaptureCapacity &&
        point.minimum_code > 0U && point.maximum_code < AdcMax && point.minimum_code <= point.maximum_code &&
        std::uint32_t{point.maximum_code} - point.minimum_code <= 32U && point.rail_samples == 0U &&
        point.mean_code_q16 >= std::uint32_t{point.minimum_code} * 65536U &&
        point.mean_code_q16 <= std::uint32_t{point.maximum_code} * 65536U &&
        point.reference_microvolts >= -MaximumCalibrationReferenceMicrovolts &&
        point.reference_microvolts <= MaximumCalibrationReferenceMicrovolts;
}

std::int64_t round_div(std::int64_t numerator, std::uint64_t denominator) noexcept
{
    const auto magnitude = static_cast<std::uint64_t>(numerator < 0 ? -numerator : numerator);
    const auto divided = detail::divide_u64(magnitude, denominator);
    const auto rounded = divided.quotient +
        (divided.remainder >= (denominator + 1U) / 2U ? 1U : 0U);
    return numerator < 0 ? -static_cast<std::int64_t>(rounded) : static_cast<std::int64_t>(rounded);
}

std::int64_t prediction(std::uint32_t mean, VoltageCalibration profile) noexcept
{
    const auto delta = std::int64_t{mean} - profile.zero_code_q16;
    return detail::round_gain_product(delta * profile.microvolts_per_code_q16,
        profile.gain_shift);
}

std::uint64_t difference(std::int64_t a, std::int64_t b) noexcept
{
    const auto delta = a - b;
    return static_cast<std::uint64_t>(delta < 0 ? -delta : delta);
}

bool overlaps(const void* first, std::size_t first_size, const void* second, std::size_t second_size) noexcept
{
    const auto a = reinterpret_cast<std::uintptr_t>(first);
    const auto b = reinterpret_cast<std::uintptr_t>(second);
    return a <= b ? b - a < first_size : a - b < second_size;
}
}

bool calibration_valid(VoltageCalibration calibration) noexcept
{
    return (calibration.kind == CalibrationKind::Nominal || calibration.kind == CalibrationKind::Measured) &&
        calibration_contract::profile_values_valid(calibration.zero_code_q16,
            calibration.microvolts_per_code_q16, calibration.gain_shift);
}

bool code_to_microvolts(std::uint16_t code, VoltageCalibration calibration,
                        std::int64_t& microvolts) noexcept
{
    if (code > AdcMax || !calibration_valid(calibration)) { return false; }
    const auto delta = std::int64_t{code} * 65536 - calibration.zero_code_q16;
    // |delta|<=268369920, gain<=UINT32_MAX: product<2^60, including rounding.
    microvolts = detail::round_gain_product(delta * calibration.microvolts_per_code_q16,
        calibration.gain_shift);
    return true;
}

bool calibration_physical_valid(const PhysicalSettings& physical) noexcept
{
    return slot(physical) >= 0;
}

bool calibration_point_valid(const CalibrationPoint& point) noexcept
{
    return valid_point(point);
}

bool CalibrationFit::begin(const CalibrationPoint& first, const CalibrationPoint& second) noexcept
{
    status_ = CalibrationStatus::InvalidPoint;
    profile_ = VoltageCalibration{};
    validation_error_ = 0U;
    if (!valid_point(first) || !valid_point(second)) { return false; }
    if (!same_physical_settings(first.physical, second.physical)) {
        status_ = CalibrationStatus::PhysicalMismatch;
        return false;
    }
    if (first.acquisition_epoch == second.acquisition_epoch) { status_ = CalibrationStatus::StaleEpoch; return false; }
    const auto delta_code = std::int64_t{second.mean_code_q16} - first.mean_code_q16;
    const auto delta_voltage = second.reference_microvolts - first.reference_microvolts;
    if (difference(second.mean_code_q16, first.mean_code_q16) < 64U * 65536U) {
        status_ = CalibrationStatus::InsufficientSpan; return false;
    }
    if (delta_voltage == 0 || (delta_code < 0) != (delta_voltage < 0)) { status_ = CalibrationStatus::InvalidSlope; return false; }
    // References are bounded to +/-100V. |deltaV|*2^32 <= 8.59e17;
    // reconstruction/zero calculations also stay below 2^60 signed. Compute
    // the floor quotient once. For shifts1/2, remainder/D is less than one
    // and cannot cross the half-step between integer quotient residues.
    // Use the floor quotient, never an already rounded shift-zero mantissa.
    std::uint32_t gain{};
    std::uint8_t shift{};
    const auto abs_delta_code = static_cast<std::uint64_t>(delta_code < 0 ? -delta_code : delta_code);
    const auto abs_delta_voltage = static_cast<std::uint64_t>(delta_voltage < 0 ? -delta_voltage : delta_voltage);
    const auto numerator = abs_delta_voltage << 32U;
    const auto divided = detail::divide_u64(numerator, abs_delta_code);
    const auto quotient = divided.quotient;
    const auto remainder = divided.remainder;
    const auto rounded_shift0 = quotient + (remainder >= (abs_delta_code + 1U) / 2U ? 1U : 0U);
    if (rounded_shift0 != 0U && rounded_shift0 <= UINT32_MAX) {
        gain = static_cast<std::uint32_t>(rounded_shift0);
    } else {
        shift = 1U;
        const auto shift1 = (quotient + 1U) >> 1U;
        if (shift1 >= 0x80000000U && shift1 <= UINT32_MAX) {
            gain = static_cast<std::uint32_t>(shift1);
        } else {
            shift = 2U;
            const auto shift2 = (quotient + 2U) >> 2U;
            if (shift2 >= 0x80000000U && shift2 <= UINT32_MAX) {
                gain = static_cast<std::uint32_t>(shift2);
            }
        }
    }
    if (gain == 0U) { status_ = CalibrationStatus::Unrepresentable; return false; }
    const auto zero = std::int64_t{first.mean_code_q16} -
        round_div(first.reference_microvolts * (std::int64_t{1U} << (32U - shift)), gain);
    if (zero < 0 || zero > MaximumCodeQ16) { status_ = CalibrationStatus::Unrepresentable; return false; }
    const VoltageCalibration candidate{static_cast<std::int32_t>(zero), gain, CalibrationKind::Measured, shift};
    // Mantissa/zero quantization stays below 2.125uV at shift2; endpoint
    // residual checks retain the established integer-microvolt limit.
    if (difference(prediction(first.mean_code_q16, candidate), first.reference_microvolts) > 2U ||
        difference(prediction(second.mean_code_q16, candidate), second.reference_microvolts) > 2U) {
        status_ = CalibrationStatus::FitResidual; return false;
    }
    physical_ = first.physical;
    epochs_ = {first.acquisition_epoch, second.acquisition_epoch};
    fit_codes_ = {first.mean_code_q16, second.mean_code_q16};
    profile_ = candidate;
    status_ = CalibrationStatus::Pending;
    return true;
}

bool CalibrationFit::validate(const CalibrationPoint& third, std::uint32_t tolerance_microvolts) noexcept
{
    if (status_ != CalibrationStatus::Pending || !valid_point(third) ||
        tolerance_microvolts > MaximumCalibrationToleranceMicrovolts) {
        status_ = CalibrationStatus::InvalidPoint; return false;
    }
    if (!same_physical_settings(physical_, third.physical)) {
        status_ = CalibrationStatus::PhysicalMismatch;
        return false;
    }
    if (third.acquisition_epoch == epochs_[0] || third.acquisition_epoch == epochs_[1]) {
        status_ = CalibrationStatus::StaleEpoch; return false;
    }
    const auto low = fit_codes_[0] < fit_codes_[1] ? fit_codes_[0] : fit_codes_[1];
    const auto high = fit_codes_[0] > fit_codes_[1] ? fit_codes_[0] : fit_codes_[1];
    // A repeat of an endpoint cannot masquerade as independent validation.
    if (third.mean_code_q16 < low + 16U * 65536U || third.mean_code_q16 > high - 16U * 65536U) {
        status_ = CalibrationStatus::InsufficientSpan; return false;
    }
    const auto error = difference(prediction(third.mean_code_q16, profile_), third.reference_microvolts);
    validation_error_ = static_cast<std::uint32_t>(error);
    if (error > tolerance_microvolts) { status_ = CalibrationStatus::ValidationFailed; return false; }
    status_ = CalibrationStatus::Verified;
    return true;
}

bool CalibrationBank::install(const CalibrationFit& fit) noexcept
{
    const auto* profile = fit.profile();
    const auto index = slot(fit.physical());
    if (board_identity_ == 0U || profile == nullptr || index < 0 ||
        fit.physical().coupling != InputCoupling::Dc || !calibration_valid(*profile) || next_revision_ == UINT32_MAX) { return false; }
    entries_[static_cast<std::size_t>(index)] = Entry{*profile, next_revision_++};
    return true;
}

VoltageCalibration CalibrationBank::resolve(const PhysicalSettings& physical) const noexcept
{
    const auto index = slot(physical);
    if (board_identity_ == 0U || index < 0) { return VoltageCalibration{}; }
    return entries_[static_cast<std::size_t>(index)].profile;
}

std::uint32_t CalibrationBank::revision(const PhysicalSettings& physical) const noexcept
{
    const auto index = slot(physical);
    return board_identity_ != 0U && index >= 0 ? entries_[static_cast<std::size_t>(index)].revision : 0U;
}

void CalibrationBank::clear() noexcept
{
    for (auto& entry : entries_) { entry = Entry{}; }
    next_revision_ = 1U;
}

std::size_t CalibrationBank::export_record(std::uint8_t* output, std::size_t capacity) const noexcept
{
    if (board_identity_ == 0U || output == nullptr) { return 0U; }
    std::uint16_t count{};
    for (const auto& entry : entries_) { if (entry.revision != 0U) { ++count; } }
    const auto size = HeaderBytes + std::size_t{count} * EntryBytes + 4U;
    if (capacity < size || overlaps(output, size, this, sizeof(*this))) { return 0U; }
    bool wide{};
    for (const auto& entry : entries_) { wide = wide || entry.profile.gain_shift != 0U; }
    output[0] = 'D'; output[1] = 'S'; output[2] = 'C'; output[3] = '1';
    binary::write_u16_le(output + 4U, wide ? 2U : 1U); binary::write_u16_le(output + 6U, count);
    binary::write_u64_le(output + 8U, board_identity_); binary::write_u32_le(output + 16U, next_revision_);
    std::size_t offset{HeaderBytes};
    for (std::size_t index{}; index < entries_.size(); ++index) {
        const auto& entry = entries_[index];
        if (entry.revision == 0U) { continue; }
        binary::write_u32_le(output + offset, PhysicalScaleCatalogueMicrovoltsPerDivision[index]);
        output[offset + 4U] = static_cast<std::uint8_t>(InputCoupling::Dc);
        output[offset + 5U] = static_cast<std::uint8_t>(CalibrationKind::Measured);
        output[offset + 6U] = entry.profile.gain_shift; output[offset + 7U] = 0U; binary::write_u32_le(output + offset + 8U, entry.revision);
        binary::write_u32_le(output + offset + 12U, static_cast<std::uint32_t>(entry.profile.zero_code_q16));
        binary::write_u32_le(output + offset + 16U, entry.profile.microvolts_per_code_q16);
        offset += EntryBytes;
    }
    binary::write_u32_le(output + offset, checksum::crc32_ieee(output, offset));
    return size;
}

bool calibration_record_valid(const std::uint8_t* input, std::size_t size, std::uint64_t expected_board) noexcept
{
    if (expected_board == 0U || input == nullptr || size < HeaderBytes + 4U || size > CalibrationRecordMaximum ||
        input[0] != 'D' || input[1] != 'S' || input[2] != 'C' || input[3] != '1' ||
        (binary::read_u16_le(input + 4U) != 1U && binary::read_u16_le(input + 4U) != 2U) || binary::read_u64_le(input + 8U) != expected_board) { return false; }
    const auto schema = binary::read_u16_le(input + 4U);
    const auto count = binary::read_u16_le(input + 6U);
    const auto next = binary::read_u32_le(input + 16U);
    if (count > CalibrationDcSlotCount || next == 0U ||
        size != HeaderBytes + std::size_t{count} * EntryBytes + 4U ||
        binary::read_u32_le(input + size - 4U) != checksum::crc32_ieee(input, size - 4U)) { return false; }
    int previous{-1}; bool wide{};
    for (std::size_t i{}; i < count; ++i) {
        const auto* entry = input + HeaderBytes + i * EntryBytes;
        const PhysicalSettings key{1U, binary::read_u32_le(entry), static_cast<InputCoupling>(entry[4])};
        const auto index = slot(key);
        const auto revision = binary::read_u32_le(entry + 8U);
        const auto zero = binary::read_u32_le(entry + 12U);
        const VoltageCalibration profile{zero <= MaximumCodeQ16 ? static_cast<std::int32_t>(zero) : -1,
            binary::read_u32_le(entry + 16U), static_cast<CalibrationKind>(entry[5]), entry[6]};
        wide = wide || profile.gain_shift != 0U;
        if (index <= previous || key.coupling != InputCoupling::Dc || entry[7] != 0U ||
            (schema == 1U && entry[6] != 0U) ||
            revision == 0U || revision >= next || profile.kind != CalibrationKind::Measured || !calibration_valid(profile)) { return false; }
        // Revisions are unique across installs. Check at most eight prior entries.
        for (std::size_t j{}; j < i; ++j) {
            if (binary::read_u32_le(input + HeaderBytes + j * EntryBytes + 8U) == revision) { return false; }
        }
        previous = index;
    }
    return schema != 2U || wide;
}

bool CalibrationBank::import_record(const std::uint8_t* input, std::size_t size) noexcept
{
    // Reject aliased bytes before a validator can inspect object storage.
    if (input == nullptr || overlaps(input, size, this, sizeof(*this)) ||
        !calibration_record_valid(input, size, board_identity_)) { return false; }
    const auto count = binary::read_u16_le(input + 6U);
    // No externally visible mutation precedes complete validation. Foreground
    // owner excludes concurrent readers; aliased input was rejected above.
    clear();
    for (std::size_t i{}; i < count; ++i) {
        const auto* entry = input + HeaderBytes + i * EntryBytes;
        const auto index = slot(PhysicalSettings{1U, binary::read_u32_le(entry), InputCoupling::Dc});
        entries_[static_cast<std::size_t>(index)] = Entry{
            {static_cast<std::int32_t>(binary::read_u32_le(entry + 12U)), binary::read_u32_le(entry + 16U), CalibrationKind::Measured, entry[6]}, binary::read_u32_le(entry + 8U)};
    }
    next_revision_ = binary::read_u32_le(input + 16U);
    return true;
}

} // namespace dso
