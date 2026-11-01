#ifndef DSO_STORAGE_HPP
#define DSO_STORAGE_HPP

#include <array>
#include <cstdint>

namespace dso {
inline constexpr std::uint16_t StoragePageBytes{1024U};
inline constexpr std::uint8_t StoragePagesPerBank{2U};
inline constexpr std::uint8_t StorageBankCount{1U};
static_assert(StorageBankCount == 1U, "SnapshotStorage scalar state requires exactly one bank");
inline constexpr std::uint16_t StorageBankBytes{2048U};
inline constexpr std::uint16_t StorageRegionBytes{StorageBankCount * StorageBankBytes};
inline constexpr std::uint16_t StorageHeaderBytes{32U};
inline constexpr std::uint16_t StoragePayloadMaximum{StorageBankBytes - StorageHeaderBytes};
inline constexpr std::uint32_t StorageMagic{0x53534f44U}; // Little-endian "DOSS".
inline constexpr std::uint16_t StorageSchema{1U};
inline constexpr std::uint16_t StorageCommit{0xa55aU};

// Relative byte addresses in one explicitly reserved bank. Every
// callback is foreground-only, bounded and nonthrowing. Only erased halfwords
// may be programmed, once, until another page erase. A false write/erase result
// may have changed ANY target bits; callers must rescan instead of resuming it.
struct StoragePort final {
    void* context{};
    bool (*read)(void*, std::uint32_t offset, std::uint8_t*, std::uint16_t bytes) noexcept{};
    bool (*program_halfword)(void*, std::uint32_t offset, std::uint16_t value) noexcept{};
    bool (*erase_page)(void*, std::uint8_t page_index) noexcept{};
};
struct StorageSource final {
    void* context{};
    bool (*read)(void*, std::uint16_t offset, std::uint8_t*, std::uint16_t bytes) noexcept{};
};
struct StorageRecord final {
    std::uint32_t generation{};
    std::uint32_t crc{};
    std::uint16_t bytes{};
    std::uint8_t bank{};
    bool valid{};
};
enum class StorageState : std::uint8_t { Idle, Scanning, Saving, Complete, Fault };
enum class StorageResult : std::uint8_t { None, Empty, Ready, NoValidRecord, Saved };
enum class StorageError : std::uint8_t {
    None, InvalidPort, InvalidRequest, NotMounted, ReadFailure, SourceFailure,
    ProgramFailure, EraseFailure, VerificationFailure, GenerationExhausted,
    UnsupportedFormat
};
struct StorageStep final {
    StorageState state;
    std::uint16_t words;
    bool erased; // An erase callback was attempted; state/error determines success.
};

// Single-bank journal envelope. A save stages the full payload in RAM before
// erasing the bank; power loss during erase/program can therefore lose the
// previous record. The instrument codec owns semantic/schema/board validation.
class SnapshotStorage final {
public:
    explicit SnapshotStorage(StoragePort port) noexcept : port_{port} {}
    SnapshotStorage(const SnapshotStorage&) = delete;
    SnapshotStorage& operator=(const SnapshotStorage&) = delete;
    [[nodiscard]] bool begin_scan() noexcept;
    [[nodiscard]] bool begin_save(StorageSource source, std::uint16_t bytes) noexcept;
    // At most word_budget word operations. Staging consumes one source read of
    // at most two bytes per word. An erase consumes one operation and returns
    // immediately; no step can erase two pages. Each program operation performs
    // erased/readback checks and programming once.
    [[nodiscard]] StorageStep step(std::uint16_t word_budget) noexcept;
    // Borrowed descriptors survive read-only use; copy them before starting a
    // new operation. Reads remain available while the payload is staged, then
    // stop when the bank is invalidated for erase. False reads may modify output.
    [[nodiscard]] const StorageRecord* candidate(std::uint8_t bank) const noexcept;
    [[nodiscard]] const StorageRecord* selected() const noexcept;
    [[nodiscard]] bool select(const StorageRecord& record) noexcept;
    [[nodiscard]] bool read(const StorageRecord& record, std::uint16_t offset,
                            std::uint8_t* output, std::uint16_t bytes) const noexcept;
    [[nodiscard]] StorageState state() const noexcept { return state_; }
    [[nodiscard]] StorageResult result() const noexcept { return result_; }
    [[nodiscard]] StorageError error() const noexcept { return error_; }
    [[nodiscard]] bool mounted() const noexcept { return mounted_; }
    [[nodiscard]] std::uint8_t damaged_banks() const noexcept { return damaged_; }
    [[nodiscard]] std::uint8_t unsupported_banks() const noexcept { return unsupported_; }

private:
    enum class Phase : std::uint8_t {
        ScanHeader, ScanBlank, ScanPayload, StagePayload, Erase, VerifyErase, WritePayload,
        WriteHeader, VerifyHeader, VerifyPayload, Commit, VerifyCommitHeader, VerifyCommitPayload
    };
    [[nodiscard]] bool port_valid(bool writing) const noexcept;
    void fail(StorageError error) noexcept;
    void next_scan_bank(bool valid, bool blank = false) noexcept;
    void inspect_header() noexcept;
    void make_header() noexcept;
    [[nodiscard]] bool read_word(std::uint32_t offset, std::uint8_t (&word)[2]) noexcept;
    [[nodiscard]] bool program_word(std::uint32_t offset, std::uint16_t value) noexcept;
    StoragePort port_;
    StorageSource source_{};
    StorageRecord record_{};
    // Only bytes_ bytes are consumed; every byte is written during staging
    // before erase or payload programming can begin.
    std::array<std::uint8_t, StoragePayloadMaximum> staged_payload_;
    std::array<std::uint8_t, StorageHeaderBytes> header_{};
    std::uint32_t crc_{0xffffffffU};
    std::uint32_t generation_{};
    std::uint16_t offset_{};
    std::uint16_t bytes_{};
    StorageState state_{StorageState::Idle};
    StorageResult result_{StorageResult::None};
    StorageError error_{StorageError::None};
    Phase phase_{Phase::ScanHeader};
    std::uint8_t page_{};
    std::uint8_t damaged_{};
    std::uint8_t unsupported_{};
    bool mounted_{};
};
} // namespace dso
#endif
