#include "dso_saved_record.hpp"
#include <array>
#include <cstddef>
#include <limits>
#include <type_traits>

namespace dso {
namespace {
constexpr std::uint32_t Magic{0x31504353U}; // "SCP1" in the canonical byte stream.
constexpr std::uint16_t Waveform{1U}, Rolling{2U}, Edge{4U}, RawClip{8U}, Clamp{16U};
constexpr std::uint16_t Calibration{32U};

// Use direct field transfers only when the host uses little-endian scalar
// representations. Every table entry names an unsigned scalar and its fixed
// canonical wire offset; enums, signed values, and reserved bytes stay explicit.
#if defined(_WIN32) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define DSO_TABLE_CODEC_LE 1
#else
#define DSO_TABLE_CODEC_LE 0
#endif
#if DSO_TABLE_CODEC_LE
static_assert(std::is_standard_layout_v<SavedRecord> && sizeof(SavedRecord) <= 255U &&
              std::is_standard_layout_v<ScopeUiSettings> && std::is_standard_layout_v<FrameMetadata> &&
              std::is_standard_layout_v<PhysicalSettings> && std::is_standard_layout_v<TimebasePlan> &&
              std::is_standard_layout_v<CaptureConfig>,
              "Canonical field offsets require bounded standard-layout value types");

struct Field final {
    std::uint8_t object;
    std::uint8_t wire;
    std::uint8_t width;
};

template <typename T, typename Expected>
constexpr bool exact_unsigned_type = std::is_same_v<T, Expected> && std::is_unsigned_v<T>;

constexpr Field field(std::size_t object, std::uint8_t wire, std::uint8_t width) noexcept
{ return {static_cast<std::uint8_t>(object), wire, width}; }

template <std::size_t N>
constexpr bool valid_fields(const Field (&fields)[N]) noexcept
{
    for (std::size_t i{}; i < N; ++i) {
        const auto f = fields[i];
        if (f.width == 0U || f.width > 8U || unsigned{f.object} + f.width > sizeof(SavedRecord) ||
            unsigned{f.wire} + f.width > SavedRecordHeaderBytes) { return false; }
        for (std::size_t j{i + 1U}; j < N; ++j) {
            const auto other = fields[j];
            if (unsigned{f.object} < unsigned{other.object} + other.width &&
                unsigned{other.object} < unsigned{f.object} + f.width) { return false; }
            if (unsigned{f.wire} < unsigned{other.wire} + other.width &&
                unsigned{other.wire} < unsigned{f.wire} + f.width) { return false; }
        }
    }
    return true;
}
template <std::size_t A, std::size_t B>
constexpr bool disjoint_fields(const Field (&first)[A], const Field (&second)[B]) noexcept
{
    for (const auto a : first) for (const auto b : second) {
        if ((unsigned{a.object} < unsigned{b.object} + b.width &&
             unsigned{b.object} < unsigned{a.object} + a.width) ||
            (unsigned{a.wire} < unsigned{b.wire} + b.width &&
             unsigned{b.wire} < unsigned{a.wire} + a.width)) { return false; }
    }
    return true;
}
static_assert(exact_unsigned_type<decltype(ScopeUiSettings::timebase_index), std::uint8_t>);
static_assert(exact_unsigned_type<decltype(ScopeUiSettings::trigger_level_code), std::uint16_t>);
static_assert(exact_unsigned_type<decltype(ScopeUiSettings::trigger_position_permille), std::uint16_t>);
constexpr Field SettingsFields[]{
    field(offsetof(SavedRecord, settings) + offsetof(ScopeUiSettings, timebase_index), 20U, 1U),
    field(offsetof(SavedRecord, settings) + offsetof(ScopeUiSettings, trigger_level_code), 24U, 2U),
    field(offsetof(SavedRecord, settings) + offsetof(ScopeUiSettings, trigger_position_permille), 26U, 2U)
};
static_assert(valid_fields(SettingsFields), "Settings object and wire ranges must be bounded and disjoint");
static_assert(exact_unsigned_type<decltype(PhysicalSettings::generation), std::uint32_t>);
static_assert(exact_unsigned_type<decltype(PhysicalSettings::microvolts_per_div), std::uint32_t>);
static_assert(exact_unsigned_type<decltype(FrameMetadata::acquisition_epoch), std::uint32_t>);
static_assert(exact_unsigned_type<decltype(FrameMetadata::trigger_sequence), std::uint32_t>);
static_assert(exact_unsigned_type<decltype(TimebasePlan::prescaler), std::uint16_t>);
static_assert(exact_unsigned_type<decltype(TimebasePlan::period), std::uint16_t>);
static_assert(exact_unsigned_type<decltype(TimebasePlan::timer_hz), std::uint32_t>);
static_assert(exact_unsigned_type<decltype(TimebasePlan::divider), std::uint32_t>);
static_assert(exact_unsigned_type<decltype(TimebasePlan::adc_hz), std::uint32_t>);
static_assert(exact_unsigned_type<decltype(TimebasePlan::trigger_cycles_x2), std::uint16_t>);
static_assert(exact_unsigned_type<decltype(CaptureConfig::length), std::uint16_t>);
static_assert(exact_unsigned_type<decltype(CaptureConfig::pretrigger), std::uint16_t>);
static_assert(exact_unsigned_type<decltype(CaptureConfig::level), std::uint16_t>);
static_assert(exact_unsigned_type<decltype(CaptureConfig::hysteresis), std::uint16_t>);
static_assert(exact_unsigned_type<decltype(CaptureConfig::auto_timeout_samples), std::uint32_t>);
static_assert(exact_unsigned_type<decltype(SavedRecord::rolling_revision), std::uint64_t>);
constexpr Field FrameFields[]{
    field(offsetof(SavedRecord, frame) + offsetof(FrameMetadata, physical) + offsetof(PhysicalSettings, generation), 32U, 4U),
    field(offsetof(SavedRecord, frame) + offsetof(FrameMetadata, physical) + offsetof(PhysicalSettings, microvolts_per_div), 36U, 4U),
    field(offsetof(SavedRecord, frame) + offsetof(FrameMetadata, acquisition_epoch), 44U, 4U),
    field(offsetof(SavedRecord, frame) + offsetof(FrameMetadata, trigger_sequence), 48U, 4U),
    field(offsetof(SavedRecord, frame) + offsetof(FrameMetadata, timing) + offsetof(TimebasePlan, prescaler), 52U, 2U),
    field(offsetof(SavedRecord, frame) + offsetof(FrameMetadata, timing) + offsetof(TimebasePlan, period), 54U, 2U),
    field(offsetof(SavedRecord, frame) + offsetof(FrameMetadata, timing) + offsetof(TimebasePlan, timer_hz), 56U, 4U),
    field(offsetof(SavedRecord, frame) + offsetof(FrameMetadata, timing) + offsetof(TimebasePlan, divider), 60U, 4U),
    field(offsetof(SavedRecord, frame) + offsetof(FrameMetadata, timing) + offsetof(TimebasePlan, adc_hz), 64U, 4U),
    field(offsetof(SavedRecord, frame) + offsetof(FrameMetadata, timing) + offsetof(TimebasePlan, trigger_cycles_x2), 68U, 2U),
    field(offsetof(SavedRecord, frame) + offsetof(FrameMetadata, capture) + offsetof(CaptureConfig, length), 70U, 2U),
    field(offsetof(SavedRecord, frame) + offsetof(FrameMetadata, capture) + offsetof(CaptureConfig, pretrigger), 72U, 2U),
    field(offsetof(SavedRecord, frame) + offsetof(FrameMetadata, capture) + offsetof(CaptureConfig, level), 74U, 2U),
    field(offsetof(SavedRecord, frame) + offsetof(FrameMetadata, capture) + offsetof(CaptureConfig, hysteresis), 76U, 2U),
    field(offsetof(SavedRecord, frame) + offsetof(FrameMetadata, capture) + offsetof(CaptureConfig, auto_timeout_samples), 80U, 4U),
    field(offsetof(SavedRecord, rolling_revision), 92U, 8U)
};
static_assert(valid_fields(FrameFields), "Frame object and wire ranges must be bounded and disjoint");
static_assert(disjoint_fields(SettingsFields, FrameFields),
              "Settings and frame object/wire fields must not overlap");

#if defined(__GNUC__)
__attribute__((noipa))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
void transfer_fields(const void* source, void* output, const Field* fields, std::size_t count, bool decode) noexcept
{
    const auto* input = static_cast<const unsigned char*>(source);
    auto* target = static_cast<unsigned char*>(output);
    for (std::size_t i{}; i < count; ++i) {
        const auto f = fields[i];
        const auto from = decode ? f.wire : f.object;
        const auto to = decode ? f.object : f.wire;
        for (unsigned byte{}; byte < f.width; ++byte) {
            target[to + byte] = input[from + byte];
        }
    }
}
template <std::size_t N>
void encode_fields(const SavedRecord& record, std::uint8_t* output, const Field (&fields)[N]) noexcept
{ transfer_fields(&record, output, fields, N, false); }

template <std::size_t N>
void decode_fields(const std::uint8_t* input, SavedRecord& record, const Field (&fields)[N]) noexcept
{ transfer_fields(input, &record, fields, N, true); }
#endif


void put16(std::uint8_t* p, std::uint16_t value) noexcept
{ p[0] = static_cast<std::uint8_t>(value); p[1] = static_cast<std::uint8_t>(value >> 8U); }
void put32(std::uint8_t* p, std::uint32_t value) noexcept
{ for (unsigned i{}; i < 4U; ++i) { p[i] = static_cast<std::uint8_t>(value >> (8U * i)); } }
void put64(std::uint8_t* p, std::uint64_t value) noexcept
{
    put32(p, static_cast<std::uint32_t>(value));
    put32(p + 4U, static_cast<std::uint32_t>(value >> 32U));
}
std::uint16_t get16(const std::uint8_t* p) noexcept
{ return static_cast<std::uint16_t>(std::uint16_t{p[0]} | static_cast<std::uint16_t>(std::uint16_t{p[1]} << 8U)); }
std::uint32_t get32(const std::uint8_t* p) noexcept
{
    return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8U) |
        (std::uint32_t{p[2]} << 16U) | (std::uint32_t{p[3]} << 24U);
}
std::uint64_t get64(const std::uint8_t* p) noexcept
{
    return std::uint64_t{get32(p)} | (std::uint64_t{get32(p + 4U)} << 32U);
}
[[maybe_unused]] std::int16_t get_signed16(const std::uint8_t* p) noexcept
{
    const auto value = get16(p);
    return static_cast<std::int16_t>(value <= 32767U ? static_cast<std::int32_t>(value) : static_cast<std::int32_t>(value) - 65536);
}
bool mode_valid(TriggerMode mode) noexcept
{ return mode == TriggerMode::Normal || mode == TriggerMode::Auto || mode == TriggerMode::Single; }
bool edge_valid(TriggerEdge edge) noexcept
{ return edge == TriggerEdge::Rising || edge == TriggerEdge::Falling; }
[[maybe_unused]] std::uint8_t mode_byte(TriggerMode mode) noexcept
{ return mode == TriggerMode::Normal ? 0U : mode == TriggerMode::Auto ? 1U : 2U; }
[[maybe_unused]] TriggerMode byte_mode(std::uint8_t byte) noexcept
{ return byte == 0U ? TriggerMode::Normal : byte == 1U ? TriggerMode::Auto : TriggerMode::Single; }
bool range_valid(std::uint32_t scale) noexcept
{
    return scale == 10000U || scale == 20000U || scale == 50000U || scale == 100000U ||
        scale == 200000U || scale == 500000U || scale == 1000000U || scale == 2000000U || scale == 5000000U;
}
bool slow_divider(std::uint32_t divider) noexcept
{
    return divider == 14062500U || divider == 28125000U || divider == 70312500U ||
        divider == 140625000U || divider == 281250000U || divider == 703125000U;
}
bool profile_valid(VoltageCalibration calibration) noexcept
{
    return calibration.kind == CalibrationKind::Unavailable || calibration_valid(calibration);
}
bool wide_profile(const SavedRecord& record) noexcept
{ return record.calibration.kind != CalibrationKind::Unavailable && record.calibration.gain_shift != 0U; }
std::uint16_t packed_sample_bytes(std::uint16_t count) noexcept
{ return static_cast<std::uint16_t>((3U * std::uint32_t{count} + 1U) / 2U); }
std::uint16_t wire_waveform_end(const SavedRecord& record, std::uint16_t schema) noexcept
{
    return static_cast<std::uint16_t>(SavedRecordHeaderBytes + (record.waveform_present ?
        (schema == 4U ? packed_sample_bytes(record.frame.capture.length) : 2U * record.frame.capture.length) : 0U));
}
std::uint16_t wire_record_size(const SavedRecord& record, std::uint16_t schema) noexcept
{ return static_cast<std::uint16_t>(wire_waveform_end(record, schema) + record.calibration_bytes); }
bool calibration_length_valid(std::uint16_t bytes) noexcept
{
    return bytes == 0U || (bytes >= 24U && bytes <= SavedRecordCalibrationMaximumBytes && (bytes - 24U) % 20U == 0U);
}
bool overlaps(const void* first, std::size_t first_size, const void* second, std::size_t second_size) noexcept
{
    const auto a = reinterpret_cast<std::uintptr_t>(first), b = reinterpret_cast<std::uintptr_t>(second);
    if (first_size > UINTPTR_MAX - a || second_size > UINTPTR_MAX - b) { return true; }
    return a < b + second_size && b < a + first_size;
}
}

bool saved_settings_valid(const ScopeUiSettings& settings) noexcept
{
    return scope_ui_settings_valid(settings, ScopeTimebaseCount);
}

bool saved_record_valid(const SavedRecord& record) noexcept
{
    if (record.board_identity == 0U || !saved_settings_valid(record.settings) ||
        (record.encoding != SavedSampleEncoding::Legacy16 && record.encoding != SavedSampleEncoding::Packed12) ||
        !calibration_length_valid(record.calibration_bytes)) { return false; }
    if (!record.waveform_present) { return !record.rolling && record.rolling_revision == 0U; }
    const auto& f = record.frame; const auto& c = f.capture; const auto& t = f.timing;
    const bool slow = slow_divider(t.divider);
    if (f.physical.generation == 0U || !range_valid(f.physical.microvolts_per_div) ||
        (f.physical.coupling != InputCoupling::Dc && f.physical.coupling != InputCoupling::Ac && f.physical.coupling != InputCoupling::Ground) ||
        f.acquisition_epoch == 0U || c.length == 0U || c.length > CaptureCapacity || c.pretrigger >= c.length ||
        c.level > AdcMax || c.hysteresis > c.level || c.hysteresis > AdcMax - c.level ||
        !mode_valid(c.mode) || !edge_valid(c.edge) || !profile_valid(record.calibration) ||
        t.timer_hz != 72000000U || t.adc_hz != 12000000U || t.trigger_cycles_x2 != 46U ||
        (std::uint64_t{t.prescaler} + 1U) * (std::uint64_t{t.period} + 1U) != t.divider ||
        (!slow && (t.divider < 144U || t.divider > 3515625U)) || (slow && c.length > 512U)) { return false; }
    if (f.processing.kind == SampleProcessingKind::Filtered25) {
        if (!slow) { return false; }
    } else if (f.processing.kind != SampleProcessingKind::Point || f.processing.source_clipped || f.processing.output_clamped) { return false; }
    if (record.rolling) {
        return slow && c.mode == TriggerMode::Auto && !f.edge_triggered && c.pretrigger == c.length - 1U &&
            record.rolling_revision >= c.length && f.trigger_sequence == static_cast<std::uint32_t>(record.rolling_revision - 1U) &&
            c.auto_timeout_samples == 0U;
    }
    return record.rolling_revision == 0U &&
        (slow ? c.mode != TriggerMode::Auto : c.mode != TriggerMode::Auto || c.auto_timeout_samples > c.pretrigger) &&
        (f.edge_triggered || c.mode == TriggerMode::Auto);
}

std::uint16_t saved_record_size(const SavedRecord& record) noexcept
{
    const auto end = saved_waveform_end(record);
    return end == 0U ? 0U : static_cast<std::uint16_t>(end + record.calibration_bytes);
}

std::uint16_t saved_waveform_end(const SavedRecord& record) noexcept
{
    return !saved_record_valid(record) ? 0U : static_cast<std::uint16_t>(SavedRecordHeaderBytes +
        (record.waveform_present ? (record.encoding == SavedSampleEncoding::Packed12 ?
            packed_sample_bytes(record.frame.capture.length) : 2U * record.frame.capture.length) : 0U));
}

bool encode_saved_header(const SavedRecord& record, std::uint8_t* output, std::uint16_t capacity) noexcept
{
    const auto size = saved_record_size(record);
    if (output == nullptr || capacity < SavedRecordHeaderBytes || size == 0U ||
        overlaps(output, SavedRecordHeaderBytes, &record, sizeof(record))) { return false; }
    for (std::uint16_t i{}; i < SavedRecordHeaderBytes; ++i) { output[i] = 0U; }
    const auto schema = !record.waveform_present ? (record.calibration_bytes == 0U ? 1U : 2U) :
        (record.encoding == SavedSampleEncoding::Packed12 ? 4U :
            (wide_profile(record) ? 3U : (record.calibration_bytes == 0U ? 1U : 2U)));
    put32(output, Magic); put16(output + 4U, static_cast<std::uint16_t>(schema));
    put16(output + 6U, SavedRecordHeaderBytes);
    put16(output + 8U, size); put64(output + 12U, record.board_identity);
    const auto calibration_flag = record.calibration_bytes != 0U ? Calibration : 0U;
    put16(output + 10U, static_cast<std::uint16_t>(calibration_flag));
    put16(output + 100U, record.calibration_bytes);
#if DSO_TABLE_CODEC_LE
    encode_fields(record, output, SettingsFields);
    const auto& s = record.settings;
    output[21] = mode_byte(s.mode);
    output[22] = s.edge == TriggerEdge::Rising ? 0U : 1U;
    output[23] = static_cast<std::uint8_t>(s.measurement_view);
    put16(output + 28U, static_cast<std::uint16_t>(s.vertical_offset_pixels));
    put16(output + 30U, static_cast<std::uint16_t>(s.horizontal_offset_samples));
#else
    const auto& s = record.settings;
    output[20] = s.timebase_index; output[21] = mode_byte(s.mode);
    output[22] = s.edge == TriggerEdge::Rising ? 0U : 1U;
    output[23] = static_cast<std::uint8_t>(s.measurement_view);
    put16(output + 24U, s.trigger_level_code); put16(output + 26U, s.trigger_position_permille);
    put16(output + 28U, static_cast<std::uint16_t>(s.vertical_offset_pixels));
    put16(output + 30U, static_cast<std::uint16_t>(s.horizontal_offset_samples));
#endif
    if (!record.waveform_present) { return true; }
    const auto& f = record.frame;
#if !DSO_TABLE_CODEC_LE
    const auto& c = f.capture; const auto& t = f.timing;
#endif
    put16(output + 10U, static_cast<std::uint16_t>(calibration_flag | Waveform | (record.rolling ? Rolling : 0U) |
        (f.edge_triggered ? Edge : 0U) | (f.processing.source_clipped ? RawClip : 0U) | (f.processing.output_clamped ? Clamp : 0U)));
#if DSO_TABLE_CODEC_LE
    encode_fields(record, output, FrameFields);
    output[40] = static_cast<std::uint8_t>(f.physical.coupling);
    output[41] = static_cast<std::uint8_t>(f.processing.kind);
    output[42] = static_cast<std::uint8_t>(record.calibration.kind);
    output[43] = record.calibration.kind == CalibrationKind::Unavailable ? 0U : record.calibration.gain_shift;
    output[78] = mode_byte(f.capture.mode);
    output[79] = f.capture.edge == TriggerEdge::Rising ? 0U : 1U;
    if (record.calibration.kind != CalibrationKind::Unavailable) {
        put32(output + 84U, static_cast<std::uint32_t>(record.calibration.zero_code_q16));
        put32(output + 88U, record.calibration.microvolts_per_code_q16);
    }
#else
    put32(output + 32U, f.physical.generation); put32(output + 36U, f.physical.microvolts_per_div);
    output[40] = static_cast<std::uint8_t>(f.physical.coupling);
    output[41] = static_cast<std::uint8_t>(f.processing.kind);
    output[42] = static_cast<std::uint8_t>(record.calibration.kind);
    output[43] = record.calibration.kind == CalibrationKind::Unavailable ? 0U : record.calibration.gain_shift;
    put32(output + 44U, f.acquisition_epoch); put32(output + 48U, f.trigger_sequence);
    put16(output + 52U, t.prescaler); put16(output + 54U, t.period);
    put32(output + 56U, t.timer_hz); put32(output + 60U, t.divider); put32(output + 64U, t.adc_hz);
    put16(output + 68U, t.trigger_cycles_x2); put16(output + 70U, c.length);
    put16(output + 72U, c.pretrigger); put16(output + 74U, c.level); put16(output + 76U, c.hysteresis);
    output[78] = mode_byte(c.mode); output[79] = c.edge == TriggerEdge::Rising ? 0U : 1U;
    put32(output + 80U, c.auto_timeout_samples);
    if (record.calibration.kind != CalibrationKind::Unavailable) {
        put32(output + 84U, static_cast<std::uint32_t>(record.calibration.zero_code_q16));
        put32(output + 88U, record.calibration.microvolts_per_code_q16);
    }
    put64(output + 92U, record.rolling_revision);
#endif
    return true;
}

bool decode_saved_header(const std::uint8_t* input, std::uint16_t size, std::uint64_t expected_board, SavedRecord& output) noexcept
{
    if (input == nullptr || size < SavedRecordHeaderBytes || expected_board == 0U || get32(input) != Magic ||
        (get16(input + 4U) < 1U || get16(input + 4U) > 4U) || get16(input + 6U) != SavedRecordHeaderBytes ||
        get64(input + 12U) != expected_board || input[21] > 2U || input[22] > 1U || input[23] > 3U) { return false; }
    const auto flags = get16(input + 10U);
    const auto calibration_bytes = get16(input + 100U);
    const bool has_calibration = (flags & Calibration) != 0U;
    const auto schema = get16(input + 4U);
    if ((flags & ~63U) != 0U || (schema < 3U && input[43] != 0U) ||
        (schema == 1U ? has_calibration || calibration_bytes != 0U :
            schema == 2U ? !has_calibration || calibration_bytes == 0U :
                ((has_calibration != (calibration_bytes != 0U)) || input[43] > 2U ||
                    (schema == 3U && input[43] == 0U) ||
                    (schema == 4U && (flags & Waveform) == 0U))) ||
        !calibration_length_valid(calibration_bytes)) { return false; }
    for (std::uint16_t i{102U}; i < SavedRecordHeaderBytes; ++i) { if (input[i] != 0U) { return false; } }
    SavedRecord candidate{};
    candidate.board_identity = expected_board;
    candidate.calibration_bytes = calibration_bytes;
    candidate.encoding = schema == 4U ? SavedSampleEncoding::Packed12 : SavedSampleEncoding::Legacy16;
#if DSO_TABLE_CODEC_LE
    decode_fields(input, candidate, SettingsFields);
    candidate.settings.mode = byte_mode(input[21]);
    candidate.settings.edge = input[22] == 0U ? TriggerEdge::Rising : TriggerEdge::Falling;
    candidate.settings.measurement_view = static_cast<MeasurementView>(input[23]);
    candidate.settings.vertical_offset_pixels = get_signed16(input + 28U);
    candidate.settings.horizontal_offset_samples = get_signed16(input + 30U);
#else
    candidate.settings = {input[20], byte_mode(input[21]), input[22] == 0U ? TriggerEdge::Rising : TriggerEdge::Falling,
        get16(input + 24U), get16(input + 26U), get_signed16(input + 28U), get_signed16(input + 30U),
        static_cast<MeasurementView>(input[23])};
#endif
    candidate.waveform_present = (flags & Waveform) != 0U;
    if ((schema == 3U || schema == 4U) && !candidate.waveform_present) { return false; }
    if (!candidate.waveform_present) {
        if ((flags & ~static_cast<unsigned>(Calibration)) != 0U) { return false; }
        for (std::uint16_t i{32U}; i < 100U; ++i) { if (input[i] != 0U) { return false; } }
    } else {
        if (input[40] < 1U || input[40] > 3U || input[41] > 1U || input[42] > 2U || input[78] > 2U || input[79] > 1U ||
            get32(input + 84U) > std::uint32_t{AdcMax} * 65536U) { return false; }
#if DSO_TABLE_CODEC_LE
        candidate.rolling = (flags & Rolling) != 0U;
        decode_fields(input, candidate, FrameFields);
        candidate.frame.physical.coupling = static_cast<InputCoupling>(input[40]);
        candidate.frame.processing.kind = static_cast<SampleProcessingKind>(input[41]);
        candidate.frame.capture.mode = byte_mode(input[78]);
        candidate.frame.capture.edge = input[79] == 0U ? TriggerEdge::Rising : TriggerEdge::Falling;
        candidate.calibration = {static_cast<std::int32_t>(get32(input + 84U)), get32(input + 88U),
            static_cast<CalibrationKind>(input[42]), input[43]};
        candidate.frame.edge_triggered = (flags & Edge) != 0U;
        candidate.frame.processing.source_clipped = (flags & RawClip) != 0U;
        candidate.frame.processing.output_clamped = (flags & Clamp) != 0U;
#else
        candidate.rolling = (flags & Rolling) != 0U; candidate.rolling_revision = get64(input + 92U);
        auto& f = candidate.frame;
        f.physical.generation = get32(input + 32U);
        f.physical.microvolts_per_div = get32(input + 36U);
        f.physical.coupling = static_cast<InputCoupling>(input[40]);
        f.acquisition_epoch = get32(input + 44U); f.trigger_sequence = get32(input + 48U);
        f.timing.prescaler = get16(input + 52U); f.timing.period = get16(input + 54U);
        f.timing.timer_hz = get32(input + 56U); f.timing.divider = get32(input + 60U);
        f.timing.adc_hz = get32(input + 64U); f.timing.trigger_cycles_x2 = get16(input + 68U);
        f.capture.length = get16(input + 70U); f.capture.pretrigger = get16(input + 72U);
        f.capture.level = get16(input + 74U); f.capture.hysteresis = get16(input + 76U);
        f.capture.mode = byte_mode(input[78]);
        f.capture.edge = input[79] == 0U ? TriggerEdge::Rising : TriggerEdge::Falling;
        f.capture.auto_timeout_samples = get32(input + 80U);
        f.edge_triggered = (flags & Edge) != 0U;
        f.processing.kind = static_cast<SampleProcessingKind>(input[41]);
        f.processing.source_clipped = (flags & RawClip) != 0U;
        f.processing.output_clamped = (flags & Clamp) != 0U;
        candidate.calibration = {static_cast<std::int32_t>(get32(input + 84U)), get32(input + 88U), static_cast<CalibrationKind>(input[42]), input[43]};
#endif
        if (schema == 3U && (candidate.calibration.kind == CalibrationKind::Unavailable || candidate.calibration.gain_shift == 0U)) { return false; }
        if (schema == 4U && candidate.calibration.kind == CalibrationKind::Unavailable && candidate.calibration.gain_shift != 0U) { return false; }
        if (candidate.calibration.kind == CalibrationKind::Unavailable &&
            (candidate.calibration.zero_code_q16 != 0 || candidate.calibration.microvolts_per_code_q16 != 0U)) { return false; }
    }
    if (!saved_record_valid(candidate) || get16(input + 8U) != wire_record_size(candidate, schema)) { return false; }
    output = candidate;
    return true;
}

bool read_saved_record(const SavedRecord& record, SavedSampleSource samples, std::uint16_t offset,
                       std::uint8_t* output, std::uint16_t count, SavedByteSource calibration) noexcept
{
    const auto size = saved_record_size(record);
    const auto waveform_end = size == 0U ? 0U : static_cast<std::uint16_t>(size - record.calibration_bytes);
    const auto end = std::uint32_t{offset} + count;
    if (size == 0U || offset > size || count > size - offset || (count != 0U && output == nullptr) ||
        (count != 0U && overlaps(output, count, &record, sizeof(record))) ||
        (count != 0U && record.waveform_present && offset < waveform_end && end > SavedRecordHeaderBytes && samples.read == nullptr) ||
        (count != 0U && end > waveform_end && calibration.read == nullptr)) { return false; }
    if (count == 0U) { return true; }
    std::uint16_t copied{};
    if (offset < SavedRecordHeaderBytes) {
        std::array<std::uint8_t, SavedRecordHeaderBytes> header{};
        if (!encode_saved_header(record, header.data(), static_cast<std::uint16_t>(header.size()))) { return false; }
        while (copied < count && offset + copied < SavedRecordHeaderBytes) { output[copied] = header[offset + copied]; ++copied; }
    }
    while (copied < count && std::uint32_t{offset} + copied < waveform_end) {
        const auto position = static_cast<std::uint16_t>(offset + copied - SavedRecordHeaderBytes);
        std::uint8_t byte{};
        if (record.encoding == SavedSampleEncoding::Legacy16) {
            const auto index = static_cast<std::uint16_t>(position / 2U);
            std::uint16_t sample{};
            if (!samples.read(samples.context, index, sample) || sample > AdcMax) { return false; }
            byte = static_cast<std::uint8_t>((position & 1U) == 0U ? sample : sample >> 8U);
        } else {
            const auto pair = static_cast<std::uint16_t>(position / 3U);
            const auto lane = static_cast<std::uint16_t>(position % 3U);
            const auto first_index = static_cast<std::uint16_t>(2U * pair);
            std::uint16_t first{};
            if (!samples.read(samples.context, first_index, first) || first > AdcMax) { return false; }
            if (lane == 0U) { byte = static_cast<std::uint8_t>(first); }
            else {
                std::uint16_t second{};
                if (first_index + 1U < record.frame.capture.length &&
                    (!samples.read(samples.context, static_cast<std::uint16_t>(first_index + 1U), second) || second > AdcMax)) { return false; }
                if (lane == 1U) {
                    byte = static_cast<std::uint8_t>(((first >> 8U) & 0x0fU) | ((second & 0x0fU) << 4U));
                } else { byte = static_cast<std::uint8_t>(second >> 4U); }
            }
        }
        output[copied++] = byte;
    }
    if (copied < count && !calibration.read(calibration.context,
        static_cast<std::uint16_t>(offset + copied - waveform_end), output + copied,
        static_cast<std::uint16_t>(count - copied))) { return false; }
    return true;
}

bool read_saved_sample(const SavedRecord& record, SavedByteSource source, std::uint16_t index, std::uint16_t& sample) noexcept
{
    if (!saved_record_valid(record) || !record.waveform_present || index >= record.frame.capture.length || source.read == nullptr) { return false; }
    std::uint8_t bytes[2]{};
    // The caller supplies metadata decoded from this immutable byte stream.
    // Its validated encoding is authoritative; rereading the schema for every
    // indexed access would duplicate header I/O without adding validation.
    const bool packed = record.encoding == SavedSampleEncoding::Packed12;
    const auto offset = static_cast<std::uint16_t>(SavedRecordHeaderBytes +
        (packed ? 3U * (index / 2U) + (index % 2U) : 2U * index));
    if (!source.read(source.context, offset, bytes, 2U)) { return false; }
    std::uint16_t value{};
    if (!packed) { value = get16(bytes); }
    else if ((index & 1U) == 0U) {
        if (index + 1U == record.frame.capture.length && (bytes[1] & 0xf0U) != 0U) { return false; }
        value = static_cast<std::uint16_t>(bytes[0] | (static_cast<std::uint16_t>(bytes[1] & 0x0fU) << 8U));
    } else { value = static_cast<std::uint16_t>((bytes[0] >> 4U) | (std::uint16_t{bytes[1]} << 4U)); }
    if (value > AdcMax) { return false; }
    sample = value;
    return true;
}

static_assert(SavedRecordMaximumBytes == 1868U && SavedRecordLegacyMaximumBytes == 2380U,
              "Keep packed current and legacy raw record bounds explicit");
} // namespace dso
