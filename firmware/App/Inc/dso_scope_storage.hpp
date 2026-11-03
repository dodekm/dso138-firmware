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
enum class ScopeStorageError : std::uint8_t { None, Disabled, ReadOnly, InvalidRecord, UnsupportedFormat, Journal, ReadFailure };
struct ScopeStorageStep final { ScopeStorageState state; std::uint16_t words; bool erased; };

// Semantic settings/calibration layer. The journal checks each complete current
// payload CRC before this reader validates its settings header and calibration.
// CRC detects corruption; it does not authenticate the source.
class ScopeStorage final {
public:
    ScopeStorage(SnapshotStorage& journal, std::uint64_t board_identity, bool writable) noexcept;
    ScopeStorage(const ScopeStorage&) = delete;
    ScopeStorage& operator=(const ScopeStorage&) = delete;
    [[nodiscard]] bool begin_open() noexcept;
    // Calibration bytes are validated and staged before the journal mutates.
    // With a null source, an equal-sized selected calibration bank is preserved.
    [[nodiscard]] bool begin_save(const SavedRecord& record, SavedByteSource calibration = {}) noexcept;
    [[nodiscard]] ScopeStorageStep step(std::uint16_t word_budget) noexcept;
    [[nodiscard]] const SavedRecord* record() const noexcept { return published_ ? &record_ : nullptr; }
    [[nodiscard]] bool read_selected_calibration(std::uint16_t offset, std::uint8_t* output,
                                                std::uint16_t count) const noexcept;
    [[nodiscard]] ScopeStorageState state() const noexcept { return state_; }
    [[nodiscard]] ScopeStorageResult result() const noexcept { return result_; }
    [[nodiscard]] ScopeStorageError error() const noexcept { return error_; }
    [[nodiscard]] StorageError journal_error() const noexcept { return journal_.error(); }
    [[nodiscard]] std::uint64_t board_identity() const noexcept { return board_identity_; }
    [[nodiscard]] bool writable() const noexcept { return writable_ && board_identity_ != 0U; }

private:
    enum class Phase : std::uint8_t { Scan, CandidatePrefix, CandidateHeader, CandidateCalibration };
    static bool produce(void* context, std::uint16_t offset, std::uint8_t* output, std::uint16_t count) noexcept;
    void fail(ScopeStorageError error) noexcept;
    void finish_without_record() noexcept;
    void candidate_valid() noexcept;
    void inspect_header(const StorageRecord& candidate) noexcept;
    SnapshotStorage& journal_;
    const std::uint64_t board_identity_;
    SavedRecord record_;
    // These scratch buffers are filled before any saved bytes are consumed.
    std::array<std::uint8_t, SavedRecordHeaderBytes> header_;
    std::array<std::uint8_t, SavedRecordCalibrationMaximumBytes> calibration_;
    std::uint16_t offset_{};
    std::uint16_t header_bytes_{};
    ScopeStorageState state_;
    ScopeStorageResult result_{ScopeStorageResult::None};
    ScopeStorageError error_{ScopeStorageError::None};
    Phase phase_{Phase::Scan};
    bool writable_{};
    bool unsupported_{};
    bool published_{};
};
static_assert(SavedRecordMaximumBytes == 240U && SavedRecordMaximumBytes <= StoragePayloadMaximum,
              "Settings and nine DC calibration slots must fit one storage payload");
static_assert(sizeof(ScopeStorage) <= 352U, "Storage state exceeds the reduced page workspace budget");
} // namespace dso
#endif
