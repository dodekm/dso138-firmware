#include "dso_saved_record.hpp"
#include "dso_scaling.hpp"
#include <cstddef>
#include <cstdint>

namespace dso {
namespace {
constexpr std::uint32_t Magic{0x31504353U}; // SCP1
constexpr std::uint16_t HeaderCrcOffset{32U};
void put16(std::uint8_t* out, std::uint16_t value) noexcept
{ out[0] = static_cast<std::uint8_t>(value); out[1] = static_cast<std::uint8_t>(value >> 8U); }
void put32(std::uint8_t* out, std::uint32_t value) noexcept
{ for (unsigned i{}; i < 4U; ++i) { out[i] = static_cast<std::uint8_t>(value >> (8U * i)); } }
void put64(std::uint8_t* out, std::uint64_t value) noexcept
{ put32(out, static_cast<std::uint32_t>(value)); put32(out + 4U, static_cast<std::uint32_t>(value >> 32U)); }
std::uint16_t get16(const std::uint8_t* in) noexcept
{ return static_cast<std::uint16_t>(std::uint16_t{in[0]} | static_cast<std::uint16_t>(std::uint16_t{in[1]} << 8U)); }
std::uint64_t get64(const std::uint8_t* in) noexcept
{
    const auto low = std::uint32_t{in[0]} | (std::uint32_t{in[1]} << 8U) |
        (std::uint32_t{in[2]} << 16U) | (std::uint32_t{in[3]} << 24U);
    const auto high = std::uint32_t{in[4]} | (std::uint32_t{in[5]} << 8U) |
        (std::uint32_t{in[6]} << 16U) | (std::uint32_t{in[7]} << 24U);
    return std::uint64_t{low} | (std::uint64_t{high} << 32U);
}
std::int16_t signed16(std::uint16_t value) noexcept
{ return static_cast<std::int16_t>(value <= 32767U ? static_cast<std::int32_t>(value) : static_cast<std::int32_t>(value) - 65536); }
std::uint32_t crc32(const std::uint8_t* bytes, std::uint16_t count) noexcept
{
    std::uint32_t crc{0xffffffffU};
    for (std::uint16_t i{}; i < count; ++i) {
        crc ^= bytes[i];
        for (unsigned bit{}; bit < 8U; ++bit) { crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xedb88320U : 0U); }
    }
    return crc ^ 0xffffffffU;
}
std::uint8_t mode_code(TriggerMode mode) noexcept
{ return mode == TriggerMode::Normal ? 0U : mode == TriggerMode::Auto ? 1U : 2U; }
bool mode_valid(std::uint8_t mode) noexcept { return mode <= 2U; }
bool edge_valid(std::uint8_t edge) noexcept { return edge <= 1U; }
bool view_valid(std::uint8_t view) noexcept { return view <= static_cast<std::uint8_t>(MeasurementView::Frequency); }
bool calibration_length_valid(std::uint16_t bytes) noexcept
{ return bytes == 0U || (bytes >= 24U && bytes <= SavedRecordCalibrationMaximumBytes && (bytes - 24U) % 20U == 0U); }
bool overlaps(const void* first, std::size_t first_size, const void* second, std::size_t second_size) noexcept
{
    const auto a = reinterpret_cast<std::uintptr_t>(first), b = reinterpret_cast<std::uintptr_t>(second);
    if (first_size > UINTPTR_MAX - a || second_size > UINTPTR_MAX - b) { return true; }
    return a < b + second_size && b < a + first_size;
}
}

bool saved_settings_valid(const ScopeUiSettings& settings) noexcept
{
    return scope_ui_settings_valid(settings, ScopeTimebaseCount) &&
        static_cast<std::uint8_t>(settings.measurement_view) <= static_cast<std::uint8_t>(MeasurementView::Frequency);
}
bool saved_record_valid(const SavedRecord& record) noexcept
{ return record.board_identity != 0U && saved_settings_valid(record.settings) && calibration_length_valid(record.calibration_bytes); }
std::uint16_t saved_record_size(const SavedRecord& record) noexcept
{ return saved_record_valid(record) ? static_cast<std::uint16_t>(SavedRecordHeaderBytes + record.calibration_bytes) : 0U; }

bool encode_saved_header(const SavedRecord& record, std::uint8_t* output, std::uint16_t capacity) noexcept
{
    if (output == nullptr || capacity < SavedRecordHeaderBytes || saved_record_size(record) == 0U ||
        overlaps(output, SavedRecordHeaderBytes, &record, sizeof(record))) { return false; }
    for (std::uint16_t i{}; i < SavedRecordHeaderBytes; ++i) { output[i] = 0U; }
    put32(output, Magic); put16(output + 4U, SavedRecordCurrentSchema); put16(output + 6U, SavedRecordHeaderBytes);
    put64(output + 8U, record.board_identity);
    output[16] = record.settings.timebase_index; output[17] = mode_code(record.settings.mode);
    output[18] = record.settings.edge == TriggerEdge::Rising ? 0U : 1U;
    output[19] = static_cast<std::uint8_t>(record.settings.measurement_view);
    put16(output + 20U, record.settings.trigger_level_code); put16(output + 22U, record.settings.trigger_position_permille);
    put16(output + 24U, static_cast<std::uint16_t>(record.settings.vertical_offset_pixels));
    put16(output + 26U, static_cast<std::uint16_t>(record.settings.horizontal_offset_samples));
    put16(output + 28U, record.calibration_bytes);
    put32(output + HeaderCrcOffset, crc32(output, HeaderCrcOffset));
    return true;
}

bool decode_saved_header(const std::uint8_t* input, std::uint16_t size, std::uint64_t expected_board,
                         SavedRecord& output) noexcept
{
    if (input == nullptr || size != SavedRecordHeaderBytes || expected_board == 0U ||
        input[0] != 'S' || input[1] != 'C' || input[2] != 'P' || input[3] != '1' ||
        get16(input + 4U) != SavedRecordCurrentSchema || get16(input + 6U) != SavedRecordHeaderBytes ||
        get64(input + 8U) != expected_board || get16(input + 30U) != 0U ||
        std::uint32_t{input[32]} + (std::uint32_t{input[33]} << 8U) +
            (std::uint32_t{input[34]} << 16U) + (std::uint32_t{input[35]} << 24U) != crc32(input, HeaderCrcOffset)) { return false; }
    const auto mode = input[17], edge = input[18], view = input[19];
    if (!mode_valid(mode) || !edge_valid(edge) || !view_valid(view)) { return false; }
    SavedRecord candidate{};
    candidate.board_identity = expected_board;
    candidate.settings = {input[16], mode == 0U ? TriggerMode::Normal : mode == 1U ? TriggerMode::Auto : TriggerMode::Single,
        edge == 0U ? TriggerEdge::Rising : TriggerEdge::Falling, get16(input + 20U), get16(input + 22U),
        signed16(get16(input + 24U)), signed16(get16(input + 26U)), static_cast<MeasurementView>(view)};
    candidate.calibration_bytes = get16(input + 28U);
    if (!saved_record_valid(candidate)) { return false; }
    output = candidate;
    return true;
}
} // namespace dso
