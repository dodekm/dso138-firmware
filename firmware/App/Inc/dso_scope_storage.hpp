#ifndef DSO_SCOPE_STORAGE_HPP
#define DSO_SCOPE_STORAGE_HPP

#include "dso_saved_record.hpp"
#include "dso_storage.hpp"
#include "dso_calibration.hpp"
#include <array>
#include <cstdint>

namespace dso {
enum class ScopeStorageState : std::uint8_t { Disabled, Idle, Opening, Ready, Saving, Error };
enum class ScopeStorageResult : std::uint8_t { None, Empty, Opened, NoValidRecord, Saved };
enum class ScopeStorageError : std::uint8_t {
    None, Disabled, ReadOnly, InvalidRecord, UnsupportedFormat, Journal, ReadFailure
};
struct ScopeStorageStep final { ScopeStorageState state; std::uint16_t words; bool erased; };

// Foreground-only semantic layer. The journal/port must outlive this object and
// must not be operated separately. All candidates are completely validated
// before publication. A transport read failure aborts opening; a known-format
// semantic failure permits fallback. Unknown formats inhibit all future writes.
class ScopeStorage final {
public:
    ScopeStorage(SnapshotStorage& journal, std::uint64_t board_identity, bool writable) noexcept;
    ScopeStorage(const ScopeStorage&) = delete;
    ScopeStorage& operator=(const ScopeStorage&) = delete;
    [[nodiscard]] bool begin_open() noexcept;
    // Metadata is copied into a canonical header immediately. Sample callbacks
    // remain borrowed and MUST be immutable until Saving ends. The old selected
    // record stays available throughout Saving, including as the sample source
    // of a settings-only update that preserves the existing waveform.
    // Calibration input is a separate immutable canonical DSC1 stream, never
    // the waveform's historical profile. Preflight reads at most 204 bytes in
    // <=32-byte chunks before starting any journal mutation. A failed preflight
    // after workspace reuse requires reopening before consuming old data.
    [[nodiscard]] bool begin_save(const SavedRecord& record, SavedSampleSource samples,
                                  SavedByteSource calibration = {}) noexcept;
    // Each word is one journal operation or <=2 semantic input bytes. Header
    // reads use <=32-byte chunks. Erases keep the journal's one-erase-per-step
    // contract. A zero budget performs no work.
    [[nodiscard]] ScopeStorageStep step(std::uint16_t word_budget) noexcept;
    [[nodiscard]] const SavedRecord* record() const noexcept { return published_ ? &record_ : nullptr; }
    [[nodiscard]] bool read_selected_sample(std::uint16_t index, std::uint16_t& sample) const noexcept;
    [[nodiscard]] bool read_selected_calibration(std::uint16_t offset, std::uint8_t* output,
                                                std::uint16_t count) const noexcept;
    // For a second, staged recall pass: seed includes the exact selected
    // header; update the payload CRC in source order, using packed_sample_crc12
    // for schema4 samples or sample_crc for legacy 16-bit samples, followed by
    // appended DSC1 halfwords. Compare (seed ^ 0xffffffffU) to expected before
    // publishing the copy. Ready only; no I/O. A failed save requires reopening.
    [[nodiscard]] bool selected_samples_crc_seed(std::uint32_t& seed, std::uint32_t& expected) const noexcept;
    [[nodiscard]] static std::uint32_t sample_crc(std::uint32_t seed, std::uint16_t sample) noexcept;
    [[nodiscard]] static std::uint32_t packed_sample_crc12(std::uint32_t seed, std::uint16_t first,
                                                           std::uint16_t second, bool has_second) noexcept;
    [[nodiscard]] bool selected_samples_packed() const noexcept;
    [[nodiscard]] ScopeStorageState state() const noexcept { return state_; }
    [[nodiscard]] ScopeStorageResult result() const noexcept { return result_; }
    [[nodiscard]] ScopeStorageError error() const noexcept { return error_; }
    [[nodiscard]] StorageError journal_error() const noexcept { return journal_.error(); }
    [[nodiscard]] std::uint64_t board_identity() const noexcept { return board_identity_; }
    [[nodiscard]] bool writable() const noexcept { return writable_ && !unsupported_ && board_identity_ != 0U; }

private:
    enum class Phase : std::uint8_t { Scan, CandidateHeader, CandidatePrefix, CandidateSamples, CandidateCalibration, FinalHeader };
    static bool produce(void* context, std::uint16_t offset, std::uint8_t* output, std::uint16_t count) noexcept;
    void fail(ScopeStorageError error) noexcept;
    void next_candidate() noexcept;
    void candidate_valid() noexcept;
    void samples_valid() noexcept;
    void inspect_header(const StorageRecord& candidate) noexcept;
    void finish_candidates() noexcept;
    SnapshotStorage& journal_;
    const std::uint64_t board_identity_;
    SavedRecord record_{};
    std::array<std::uint8_t, CalibrationSupportedRecordMaximum> header_{};
    SavedSampleSource samples_{};
    SavedByteSource calibration_{};
    std::uint16_t offset_{};
    ScopeStorageState state_;
    ScopeStorageResult result_{ScopeStorageResult::None};
    ScopeStorageError error_{ScopeStorageError::None};
    Phase phase_{Phase::Scan};
    std::uint8_t candidate_bank_{};
    std::uint8_t chosen_bank_{2U};
    bool writable_{};
    bool unsupported_{};
    bool published_{};
};
static_assert(sizeof(ScopeStorage) <= 448U, "Storage state exceeds the bounded workspace budget");
} // namespace dso
#endif
