#ifndef DSO_SAVED_RECORD_HPP
#define DSO_SAVED_RECORD_HPP

#include "dso_scaling.hpp"
#include "dso_ui_model.hpp"
#include <cstdint>

namespace dso {

inline constexpr std::uint16_t SavedRecordHeaderBytes{128U};
inline constexpr std::uint16_t SavedRecordCalibrationMaximumBytes{204U};
inline constexpr std::uint16_t SavedRecordLegacyMaximumBytes{
    SavedRecordHeaderBytes + 2U * CaptureCapacity + SavedRecordCalibrationMaximumBytes};
inline constexpr std::uint16_t SavedRecordMaximumBytes{
    SavedRecordHeaderBytes + (3U * CaptureCapacity + 1U) / 2U + SavedRecordCalibrationMaximumBytes};
enum class SavedSampleEncoding : std::uint8_t { Legacy16, Packed12 };

// Value metadata only. Samples remain owned by the caller; no pointer, native
// enum layout or C++ structure padding is persisted. Settings describe the
// saved view/next capture, while frame/calibration describe the old waveform.
struct SavedRecord final {
    std::uint64_t board_identity{};
    ScopeUiSettings settings{3U, TriggerMode::Auto, TriggerEdge::Rising, 2048U, 500U, 0, 0};
    FrameMetadata frame{};
    VoltageCalibration calibration{};
    std::uint64_t rolling_revision{};
    bool waveform_present{};
    bool rolling{};
    // Output transport encoding. Decoded schema1-3 records remain Legacy16;
    // new product saves explicitly select Packed12 before starting a journal.
    SavedSampleEncoding encoding{SavedSampleEncoding::Legacy16};
    // Independent canonical DSC1 section, not the historical waveform profile.
    // Zero is absent (legacy schema1); 24+20*n bytes, n=0..9, is schema2.
    std::uint16_t calibration_bytes{};
};

struct SavedSampleSource final {
    void* context{};
    bool (*read)(void*, std::uint16_t index, std::uint16_t& sample) noexcept{};
};
struct SavedByteSource final {
    void* context{};
    bool (*read)(void*, std::uint16_t offset, std::uint8_t* output, std::uint16_t count) noexcept{};
};

[[nodiscard]] bool saved_settings_valid(const ScopeUiSettings& settings) noexcept;
[[nodiscard]] bool saved_record_valid(const SavedRecord& record) noexcept;
// Zero means invalid. Otherwise the exact canonical payload extent is returned.
[[nodiscard]] std::uint16_t saved_record_size(const SavedRecord& record) noexcept;
// First byte of the independent calibration section in the canonical writer
// format, or the payload end when absent. Zero means invalid. Waveform sample
// bytes always begin at byte128; schema4 stores them pair-packed as 12-bit codes.
[[nodiscard]] std::uint16_t saved_waveform_end(const SavedRecord& record) noexcept;
// Header output is untouched on rejected input. Unused waveform fields are
// encoded as zero for a settings-only record. Unknown/reserved fields reject.
[[nodiscard]] bool encode_saved_header(const SavedRecord& record, std::uint8_t* output,
                                       std::uint16_t capacity) noexcept;
[[nodiscard]] bool decode_saved_header(const std::uint8_t* input, std::uint16_t size,
                                       std::uint64_t expected_board, SavedRecord& output) noexcept;
// Borrowed metadata, sample source and calibration source MUST stay immutable for the complete
// storage transaction. This bounded range adapter does not own/copy a waveform.
// Calibration source offsets are relative to the independent section's start;
// source bytes are opaque here and require complete DSC1 validation by owner.
// A failed source read can leave a partial range in output; caller must abort
// its unpublished storage transaction, never commit a partial stream.
[[nodiscard]] bool read_saved_record(const SavedRecord& record, SavedSampleSource samples,
                                     std::uint16_t offset, std::uint8_t* output,
                                     std::uint16_t count, SavedByteSource calibration = {}) noexcept;
// Read one 12-bit sample from an already decoded, immutable byte stream.
// Failure leaves sample unchanged. Call for EVERY sample before publication;
// the header alone cannot establish waveform validity or payload integrity.
[[nodiscard]] bool read_saved_sample(const SavedRecord& record, SavedByteSource source,
                                     std::uint16_t index, std::uint16_t& sample) noexcept;

} // namespace dso
#endif
