#include "dso_saved_record.hpp"
#include "dso_binary_endian.hpp"
#include "dso_crc32.hpp"
#include "dso_instrument_codes.hpp"
#include "dso_timebase_contract.hpp"
#include <cstddef>
#include <cstdint>

namespace dso {
namespace {
constexpr std::uint32_t Magic{0x31504353U}; // SCP1
namespace saved_header {
constexpr std::uint16_t MagicOffset{0U};
constexpr std::uint16_t SchemaOffset{4U};
constexpr std::uint16_t SizeOffset{6U};
constexpr std::uint16_t BoardIdentityOffset{8U};
constexpr std::uint16_t TimebaseOffset{16U};
constexpr std::uint16_t TriggerModeOffset{17U};
constexpr std::uint16_t TriggerEdgeOffset{18U};
constexpr std::uint16_t MeasurementViewOffset{19U};
constexpr std::uint16_t TriggerLevelOffset{20U};
constexpr std::uint16_t TriggerPositionOffset{22U};
constexpr std::uint16_t VerticalOffsetOffset{24U};
constexpr std::uint16_t HorizontalOffsetOffset{26U};
constexpr std::uint16_t CalibrationBytesOffset{28U};
constexpr std::uint16_t ReservedOffset{30U};
constexpr std::uint16_t CrcOffset{32U};
static_assert(MagicOffset == 0U && SchemaOffset == MagicOffset + 4U &&
              SizeOffset == SchemaOffset + 2U && BoardIdentityOffset == SizeOffset + 2U &&
              TimebaseOffset == BoardIdentityOffset + 8U &&
              TriggerModeOffset == TimebaseOffset + 1U &&
              TriggerEdgeOffset == TriggerModeOffset + 1U &&
              MeasurementViewOffset == TriggerEdgeOffset + 1U &&
              TriggerLevelOffset == MeasurementViewOffset + 1U &&
              TriggerPositionOffset == TriggerLevelOffset + 2U &&
              VerticalOffsetOffset == TriggerPositionOffset + 2U &&
              HorizontalOffsetOffset == VerticalOffsetOffset + 2U &&
              CalibrationBytesOffset == HorizontalOffsetOffset + 2U &&
              ReservedOffset == CalibrationBytesOffset + 2U && CrcOffset == ReservedOffset + 2U &&
              CrcOffset + 4U == SavedRecordHeaderBytes,
              "Keep the saved settings schema 5 header layout");
} // namespace saved_header
std::int16_t signed16(std::uint16_t value) noexcept
{ return static_cast<std::int16_t>(value <= 32767U ? static_cast<std::int32_t>(value) : static_cast<std::int32_t>(value) - 65536); }
std::uint8_t mode_code(TriggerMode mode) noexcept
{
    switch (mode) {
    case TriggerMode::Normal:
        return static_cast<std::uint8_t>(instrument_code_contract::TriggerMode::Normal);
    case TriggerMode::Auto:
        return static_cast<std::uint8_t>(instrument_code_contract::TriggerMode::Auto);
    case TriggerMode::Single:
        return static_cast<std::uint8_t>(instrument_code_contract::TriggerMode::Single);
    }
    return 0xffU;
}
bool mode_from_code(std::uint8_t value, TriggerMode& mode) noexcept
{
    switch (value) {
    case static_cast<std::uint8_t>(instrument_code_contract::TriggerMode::Normal):
        mode = TriggerMode::Normal;
        return true;
    case static_cast<std::uint8_t>(instrument_code_contract::TriggerMode::Auto):
        mode = TriggerMode::Auto;
        return true;
    case static_cast<std::uint8_t>(instrument_code_contract::TriggerMode::Single):
        mode = TriggerMode::Single;
        return true;
    default:
        return false;
    }
}
std::uint8_t edge_code(TriggerEdge edge) noexcept
{
    switch (edge) {
    case TriggerEdge::Rising:
        return static_cast<std::uint8_t>(instrument_code_contract::TriggerEdge::Rising);
    case TriggerEdge::Falling:
        return static_cast<std::uint8_t>(instrument_code_contract::TriggerEdge::Falling);
    }
    return 0xffU;
}
bool edge_from_code(std::uint8_t value, TriggerEdge& edge) noexcept
{
    switch (value) {
    case static_cast<std::uint8_t>(instrument_code_contract::TriggerEdge::Rising):
        edge = TriggerEdge::Rising;
        return true;
    case static_cast<std::uint8_t>(instrument_code_contract::TriggerEdge::Falling):
        edge = TriggerEdge::Falling;
        return true;
    default:
        return false;
    }
}
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
    return scope_ui_settings_valid(settings, timebase_contract::ClassicTimebaseCatalogueEntries) &&
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
    binary::write_u32_le(output + saved_header::MagicOffset, Magic);
    binary::write_u16_le(output + saved_header::SchemaOffset, SavedRecordCurrentSchema);
    binary::write_u16_le(output + saved_header::SizeOffset, SavedRecordHeaderBytes);
    binary::write_u64_le(output + saved_header::BoardIdentityOffset, record.board_identity);
    output[saved_header::TimebaseOffset] = record.settings.timebase_index;
    output[saved_header::TriggerModeOffset] = mode_code(record.settings.mode);
    output[saved_header::TriggerEdgeOffset] = edge_code(record.settings.edge);
    output[saved_header::MeasurementViewOffset] = static_cast<std::uint8_t>(record.settings.measurement_view);
    binary::write_u16_le(output + saved_header::TriggerLevelOffset, record.settings.trigger_level_code);
    binary::write_u16_le(output + saved_header::TriggerPositionOffset, record.settings.trigger_position_permille);
    binary::write_u16_le(output + saved_header::VerticalOffsetOffset,
                         static_cast<std::uint16_t>(record.settings.vertical_offset_pixels));
    binary::write_u16_le(output + saved_header::HorizontalOffsetOffset,
                         static_cast<std::uint16_t>(record.settings.horizontal_offset_samples));
    binary::write_u16_le(output + saved_header::CalibrationBytesOffset, record.calibration_bytes);
    binary::write_u32_le(output + saved_header::CrcOffset,
                         checksum::crc32_ieee(output, saved_header::CrcOffset));
    return true;
}

bool decode_saved_header(const std::uint8_t* input, std::uint16_t size, std::uint64_t expected_board,
                         SavedRecord& output) noexcept
{
    if (input == nullptr || size != SavedRecordHeaderBytes || expected_board == 0U ||
        binary::read_u32_le(input + saved_header::MagicOffset) != Magic ||
        binary::read_u16_le(input + saved_header::SchemaOffset) != SavedRecordCurrentSchema ||
        binary::read_u16_le(input + saved_header::SizeOffset) != SavedRecordHeaderBytes ||
        binary::read_u64_le(input + saved_header::BoardIdentityOffset) != expected_board ||
        binary::read_u16_le(input + saved_header::ReservedOffset) != 0U ||
        binary::read_u32_le(input + saved_header::CrcOffset) !=
            checksum::crc32_ieee(input, saved_header::CrcOffset)) { return false; }
    const auto mode = input[saved_header::TriggerModeOffset];
    const auto edge = input[saved_header::TriggerEdgeOffset];
    const auto view = input[saved_header::MeasurementViewOffset];
    TriggerMode decoded_mode{};
    TriggerEdge decoded_edge{};
    if (!mode_from_code(mode, decoded_mode) || !edge_from_code(edge, decoded_edge) ||
        !view_valid(view)) { return false; }
    SavedRecord candidate{};
    candidate.board_identity = expected_board;
    candidate.settings = {
        input[saved_header::TimebaseOffset],
        decoded_mode,
        decoded_edge,
        binary::read_u16_le(input + saved_header::TriggerLevelOffset),
        binary::read_u16_le(input + saved_header::TriggerPositionOffset),
        signed16(binary::read_u16_le(input + saved_header::VerticalOffsetOffset)),
        signed16(binary::read_u16_le(input + saved_header::HorizontalOffsetOffset)),
        static_cast<MeasurementView>(view)};
    candidate.calibration_bytes = binary::read_u16_le(input + saved_header::CalibrationBytesOffset);
    if (!saved_record_valid(candidate)) { return false; }
    output = candidate;
    return true;
}
} // namespace dso
