#include "dso_calibration.hpp"
#include "dso_voltage_math.hpp"

namespace dso {
namespace {
constexpr std::uint32_t MaximumCodeQ16{4095U * 65536U};
constexpr std::int64_t MaximumReference{100000000LL}; // +/-100 V, explicit API bound.
constexpr std::uint32_t Ranges[9]{10000U, 20000U, 50000U, 100000U, 200000U,
                                 500000U, 1000000U, 2000000U, 5000000U};
constexpr std::size_t HeaderBytes{20U};
constexpr std::size_t EntryBytes{20U};

int slot(const PhysicalSettings& physical) noexcept
{
    if (physical.generation == 0U || physical.coupling != InputCoupling::Dc) { return -1; }
    for (unsigned range{}; range < CalibrationDcSlotCount; ++range) {
        if (Ranges[range] == physical.microvolts_per_div) {
            return static_cast<int>(range);
        }
    }
    return -1;
}

bool same_physical(const PhysicalSettings& a, const PhysicalSettings& b) noexcept
{
    return a.generation == b.generation && a.microvolts_per_div == b.microvolts_per_div && a.coupling == b.coupling;
}

bool valid_point(const CalibrationPoint& point) noexcept
{
    return slot(point.physical) >= 0 && point.physical.coupling == InputCoupling::Dc &&
        point.acquisition_epoch != 0U && point.samples >= 64U && point.samples <= CaptureCapacity &&
        point.minimum_code > 0U && point.maximum_code < 4095U && point.minimum_code <= point.maximum_code &&
        std::uint32_t{point.maximum_code} - point.minimum_code <= 32U && point.rail_samples == 0U &&
        point.mean_code_q16 >= std::uint32_t{point.minimum_code} * 65536U &&
        point.mean_code_q16 <= std::uint32_t{point.maximum_code} * 65536U &&
        point.reference_microvolts >= -MaximumReference && point.reference_microvolts <= MaximumReference;
}

std::int64_t round_div(std::int64_t numerator, std::uint64_t denominator) noexcept
{
    const auto magnitude = static_cast<std::uint64_t>(numerator < 0 ? -numerator : numerator);
    const auto rounded = magnitude / denominator +
        (magnitude % denominator >= (denominator + 1U) / 2U ? 1U : 0U);
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

void put16(std::uint8_t* p, std::uint16_t value) noexcept
{ p[0] = static_cast<std::uint8_t>(value); p[1] = static_cast<std::uint8_t>(value >> 8U); }
void put32(std::uint8_t* p, std::uint32_t value) noexcept
{ for (unsigned i{}; i < 4U; ++i) { p[i] = static_cast<std::uint8_t>(value >> (i * 8U)); } }
void put64(std::uint8_t* p, std::uint64_t value) noexcept
{
    put32(p, static_cast<std::uint32_t>(value));
    put32(p + 4U, static_cast<std::uint32_t>(value >> 32U));
}
std::uint16_t get16(const std::uint8_t* p) noexcept
{ return static_cast<std::uint16_t>(p[0] | (std::uint16_t{p[1]} << 8U)); }
std::uint32_t get32(const std::uint8_t* p) noexcept
{
    return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8U) |
        (std::uint32_t{p[2]} << 16U) | (std::uint32_t{p[3]} << 24U);
}
std::uint64_t get64(const std::uint8_t* p) noexcept
{
    return std::uint64_t{get32(p)} | (std::uint64_t{get32(p + 4U)} << 32U);
}
std::uint32_t crc32(const std::uint8_t* bytes, std::size_t count) noexcept
{
    std::uint32_t crc{0xffffffffU};
    for (std::size_t i{}; i < count; ++i) {
        crc ^= bytes[i];
        for (unsigned bit{}; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xedb88320U : 0U);
        }
    }
    return crc ^ 0xffffffffU;
}
bool overlaps(const void* first, std::size_t first_size, const void* second, std::size_t second_size) noexcept
{
    const auto a = reinterpret_cast<std::uintptr_t>(first);
    const auto b = reinterpret_cast<std::uintptr_t>(second);
    return a <= b ? b - a < first_size : a - b < second_size;
}
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
    if (!same_physical(first.physical, second.physical)) { status_ = CalibrationStatus::PhysicalMismatch; return false; }
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
    const auto quotient = numerator / abs_delta_code;
    const auto remainder = numerator % abs_delta_code;
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
    if (status_ != CalibrationStatus::Pending || !valid_point(third) || tolerance_microvolts > 1000000U) {
        status_ = CalibrationStatus::InvalidPoint; return false;
    }
    if (!same_physical(physical_, third.physical)) { status_ = CalibrationStatus::PhysicalMismatch; return false; }
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
    put16(output + 4U, wide ? 2U : 1U); put16(output + 6U, count);
    put64(output + 8U, board_identity_); put32(output + 16U, next_revision_);
    std::size_t offset{HeaderBytes};
    for (std::size_t index{}; index < entries_.size(); ++index) {
        const auto& entry = entries_[index];
        if (entry.revision == 0U) { continue; }
        put32(output + offset, Ranges[index]);
        output[offset + 4U] = static_cast<std::uint8_t>(InputCoupling::Dc);
        output[offset + 5U] = static_cast<std::uint8_t>(CalibrationKind::Measured);
        output[offset + 6U] = entry.profile.gain_shift; output[offset + 7U] = 0U; put32(output + offset + 8U, entry.revision);
        put32(output + offset + 12U, static_cast<std::uint32_t>(entry.profile.zero_code_q16));
        put32(output + offset + 16U, entry.profile.microvolts_per_code_q16);
        offset += EntryBytes;
    }
    put32(output + offset, crc32(output, offset));
    return size;
}

bool calibration_record_valid(const std::uint8_t* input, std::size_t size, std::uint64_t expected_board) noexcept
{
    if (expected_board == 0U || input == nullptr || size < HeaderBytes + 4U || size > CalibrationRecordMaximum ||
        input[0] != 'D' || input[1] != 'S' || input[2] != 'C' || input[3] != '1' ||
        (get16(input + 4U) != 1U && get16(input + 4U) != 2U) || get64(input + 8U) != expected_board) { return false; }
    const auto schema = get16(input + 4U);
    const auto count = get16(input + 6U);
    const auto next = get32(input + 16U);
    if (count > CalibrationDcSlotCount || next == 0U ||
        size != HeaderBytes + std::size_t{count} * EntryBytes + 4U ||
        get32(input + size - 4U) != crc32(input, size - 4U)) { return false; }
    int previous{-1}; bool wide{};
    for (std::size_t i{}; i < count; ++i) {
        const auto* entry = input + HeaderBytes + i * EntryBytes;
        const PhysicalSettings key{1U, get32(entry), static_cast<InputCoupling>(entry[4])};
        const auto index = slot(key);
        const auto revision = get32(entry + 8U);
        const auto zero = get32(entry + 12U);
        const VoltageCalibration profile{zero <= MaximumCodeQ16 ? static_cast<std::int32_t>(zero) : -1,
            get32(entry + 16U), static_cast<CalibrationKind>(entry[5]), entry[6]};
        wide = wide || profile.gain_shift != 0U;
        if (index <= previous || key.coupling != InputCoupling::Dc || entry[7] != 0U ||
            (schema == 1U && entry[6] != 0U) ||
            revision == 0U || revision >= next || profile.kind != CalibrationKind::Measured || !calibration_valid(profile)) { return false; }
        // Revisions are unique across installs. Check at most eight prior entries.
        for (std::size_t j{}; j < i; ++j) {
            if (get32(input + HeaderBytes + j * EntryBytes + 8U) == revision) { return false; }
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
    const auto count = get16(input + 6U);
    // No externally visible mutation precedes complete validation. Foreground
    // owner excludes concurrent readers; aliased input was rejected above.
    clear();
    for (std::size_t i{}; i < count; ++i) {
        const auto* entry = input + HeaderBytes + i * EntryBytes;
        const auto index = slot(PhysicalSettings{1U, get32(entry), InputCoupling::Dc});
        entries_[static_cast<std::size_t>(index)] = Entry{
            {static_cast<std::int32_t>(get32(entry + 12U)), get32(entry + 16U), CalibrationKind::Measured, entry[6]}, get32(entry + 8U)};
    }
    next_revision_ = get32(input + 16U);
    return true;
}

} // namespace dso
