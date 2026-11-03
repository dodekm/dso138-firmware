#ifndef DSO_SAVED_RECORD_HPP
#define DSO_SAVED_RECORD_HPP

#include "dso_ui_model.hpp"
#include <cstdint>

namespace dso {
inline constexpr std::uint16_t SavedRecordHeaderBytes{36U};
inline constexpr std::uint16_t SavedRecordCalibrationMaximumBytes{204U};
inline constexpr std::uint16_t SavedRecordMaximumBytes{SavedRecordHeaderBytes + SavedRecordCalibrationMaximumBytes};
inline constexpr std::uint16_t SavedRecordCurrentSchema{5U};

// Persisted values only: current settings and the independently validated
// nine-slot DC calibration bank. Waveforms are never part of this record.
struct SavedRecord final {
    std::uint64_t board_identity{};
    ScopeUiSettings settings{3U, TriggerMode::Auto, TriggerEdge::Rising, 2048U, 500U, 0, 0};
    std::uint16_t calibration_bytes{};
};

struct SavedByteSource final {
    void* context{};
    bool (*read)(void*, std::uint16_t offset, std::uint8_t* output, std::uint16_t count) noexcept{};
};

[[nodiscard]] bool saved_settings_valid(const ScopeUiSettings& settings) noexcept;
[[nodiscard]] bool saved_record_valid(const SavedRecord& record) noexcept;
[[nodiscard]] std::uint16_t saved_record_size(const SavedRecord& record) noexcept;
[[nodiscard]] bool encode_saved_header(const SavedRecord& record, std::uint8_t* output,
                                       std::uint16_t capacity) noexcept;
[[nodiscard]] bool decode_saved_header(const std::uint8_t* input, std::uint16_t size,
                                       std::uint64_t expected_board, SavedRecord& output) noexcept;
} // namespace dso
#endif
