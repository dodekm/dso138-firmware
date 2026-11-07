#include "dso_storage.hpp"
#include "dso_binary_endian.hpp"

namespace dso {
namespace {
// Schema 2's fixed on-flash journal envelope. Change these offsets only with
// an explicit schema change; settings payload fields belong to their codec.
namespace storage_header {
constexpr std::uint16_t MagicOffset{0U};
constexpr std::uint16_t SchemaOffset{4U};
constexpr std::uint16_t HeaderBytesOffset{6U};
constexpr std::uint16_t GenerationOffset{8U};
constexpr std::uint16_t PayloadBytesOffset{12U};
constexpr std::uint16_t PayloadCrcOffset{16U};
constexpr std::uint16_t HeaderCrcOffset{20U};
constexpr std::uint16_t ReservedOffset{24U};
constexpr std::uint16_t CommitOffset{30U};
constexpr std::uint16_t HeaderCrcBytes{HeaderCrcOffset};
static_assert(SchemaOffset == MagicOffset + 4U &&
              HeaderBytesOffset == SchemaOffset + 2U &&
              GenerationOffset == HeaderBytesOffset + 2U &&
              PayloadBytesOffset == GenerationOffset + 4U &&
              PayloadCrcOffset == PayloadBytesOffset + 4U &&
              HeaderCrcOffset == PayloadCrcOffset + 4U &&
              ReservedOffset == HeaderCrcOffset + 4U &&
              CommitOffset == ReservedOffset + 6U &&
              CommitOffset + 2U == StorageHeaderBytes,
              "Journal schema 2 header offsets must remain byte-compatible");
} // namespace storage_header

bool busy(StorageState state) noexcept { return state == StorageState::Scanning || state == StorageState::Saving; }
bool same(const StorageRecord& a, const StorageRecord& b) noexcept
{ return a.valid && b.valid && a.bank == b.bank && a.bytes == b.bytes && a.crc == b.crc && a.generation == b.generation; }
}

bool SnapshotStorage::port_valid(bool writing) const noexcept
{
    return port_.read != nullptr && (!writing || (port_.program_halfword != nullptr && port_.erase_page != nullptr));
}
void SnapshotStorage::fail(StorageError error) noexcept
{
    state_ = StorageState::Fault; error_ = error; mounted_ = false;
}
bool SnapshotStorage::begin_scan() noexcept
{
    if (busy(state_)) { return false; }
    if (!port_valid(false)) { fail(StorageError::InvalidPort); return false; }
    record_ = {}; offset_ = 0U; damaged_ = 0U; unsupported_ = 0U;
    result_ = StorageResult::None; error_ = StorageError::None; mounted_ = false;
    state_ = StorageState::Scanning; phase_ = Phase::ScanHeader;
    return true;
}
bool SnapshotStorage::begin_save(StorageSource source, std::uint16_t bytes) noexcept
{
    if (busy(state_)) { return false; }
    if (!port_valid(true)) { error_ = StorageError::InvalidPort; return false; }
    if (!mounted_) { error_ = StorageError::NotMounted; return false; }
    if (source.read == nullptr || bytes == 0U || bytes > StoragePayloadWriteMaximum) { error_ = StorageError::InvalidRequest; return false; }
    const auto latest = record_.valid ? record_.generation : 0U;
    if (latest == UINT32_MAX) { error_ = StorageError::GenerationExhausted; return false; }
    generation_ = latest + 1U;
    source_ = source; bytes_ = bytes; page_ = 0U; offset_ = 0U; crc_ = checksum::Crc32IeeeInitial;
    phase_ = Phase::StagePayload; state_ = StorageState::Saving; result_ = StorageResult::None; error_ = StorageError::None;
    return true;
}

const StorageRecord* SnapshotStorage::candidate(std::uint8_t bank) const noexcept
{ return mounted_ && bank == 0U && record_.valid ? &record_ : nullptr; }
const StorageRecord* SnapshotStorage::selected() const noexcept { return candidate(0U); }
bool SnapshotStorage::read(const StorageRecord& record, std::uint16_t offset, std::uint8_t* output, std::uint16_t bytes) const noexcept
{
    if (!mounted_ || record.bank >= StorageBankCount || !same(record, record_) || offset > record.bytes ||
        bytes > record.bytes - offset || (bytes != 0U && output == nullptr)) { return false; }
    return bytes == 0U || port_.read(port_.context, StorageHeaderBytes + offset, output, bytes);
}
bool SnapshotStorage::read_word(std::uint32_t offset, std::uint8_t (&word)[2]) noexcept
{
    if (!port_.read(port_.context, offset, word, 2U)) { fail(StorageError::ReadFailure); return false; }
    return true;
}
bool SnapshotStorage::program_word(std::uint32_t offset, std::uint16_t value) noexcept
{
    std::uint8_t word[2]{};
    if (!read_word(offset, word)) { return false; }
    if (binary::read_u16_le(word) != 0xffffU) { fail(StorageError::VerificationFailure); return false; }
    if (value == 0xffffU) { return true; } // Do not program an unchanged erased word.
    if (!port_.program_halfword(port_.context, offset, value)) { fail(StorageError::ProgramFailure); return false; }
    if (!read_word(offset, word)) { return false; }
    if (binary::read_u16_le(word) != value) { fail(StorageError::VerificationFailure); return false; }
    return true;
}
void SnapshotStorage::next_scan_bank(bool valid, bool blank) noexcept
{
    record_.valid = valid;
    if (!valid) {
        record_ = {};
        if (!blank && unsupported_ == 0U) { damaged_ = 1U; }
    }
    mounted_ = true; state_ = StorageState::Complete;
    result_ = record_.valid ? StorageResult::Ready : ((damaged_ != 0U || unsupported_ != 0U) ? StorageResult::NoValidRecord : StorageResult::Empty);
    error_ = unsupported_ != 0U ? StorageError::UnsupportedFormat : StorageError::None;
}
void SnapshotStorage::inspect_header() noexcept
{
    bool blank = true;
    for (const auto byte : header_) { if (byte != 0xffU) { blank = false; break; } }
    if (blank) { phase_ = Phase::ScanBlank; offset_ = StorageHeaderBytes; return; }
    const auto committed = binary::read_u16_le(header_.data() + storage_header::CommitOffset) == StorageCommit;
    if (committed && (binary::read_u32_le(header_.data() + storage_header::MagicOffset) != StorageMagic ||
                      binary::read_u16_le(header_.data() + storage_header::SchemaOffset) != StorageSchema ||
                      binary::read_u16_le(header_.data() + storage_header::HeaderBytesOffset) != StorageHeaderBytes)) {
        unsupported_ = 1U;
        next_scan_bank(false); return;
    }
    bool padding = true;
    for (std::uint16_t i{storage_header::ReservedOffset}; i < storage_header::CommitOffset; ++i) {
        if (header_[i] != 0xffU) { padding = false; }
    }
    const auto length = binary::read_u32_le(header_.data() + storage_header::PayloadBytesOffset);
    const auto generation = binary::read_u32_le(header_.data() + storage_header::GenerationOffset);
    if (!committed || !padding || generation == 0U || length == 0U || length > StoragePayloadReadMaximum ||
        binary::read_u32_le(header_.data() + storage_header::HeaderCrcOffset) != checksum::crc32_ieee(
            header_.data(), storage_header::HeaderCrcBytes)) {
        next_scan_bank(false); return;
    }
    record_ = {generation, binary::read_u32_le(header_.data() + storage_header::PayloadCrcOffset),
               static_cast<std::uint16_t>(length), 0U, false};
    crc_ = checksum::Crc32IeeeInitial; offset_ = 0U; phase_ = Phase::ScanPayload;
}
void SnapshotStorage::make_header() noexcept
{
    header_.fill(0xffU);
    binary::write_u32_le(header_.data() + storage_header::MagicOffset, StorageMagic);
    binary::write_u16_le(header_.data() + storage_header::SchemaOffset, StorageSchema);
    binary::write_u16_le(header_.data() + storage_header::HeaderBytesOffset, StorageHeaderBytes);
    binary::write_u32_le(header_.data() + storage_header::GenerationOffset, generation_);
    binary::write_u32_le(header_.data() + storage_header::PayloadBytesOffset, bytes_);
    const auto payload_crc = checksum::crc32_ieee_finalize(crc_);
    binary::write_u32_le(header_.data() + storage_header::PayloadCrcOffset, payload_crc);
    binary::write_u32_le(header_.data() + storage_header::HeaderCrcOffset,
                         checksum::crc32_ieee(header_.data(), storage_header::HeaderCrcBytes));
    record_ = {generation_, payload_crc, bytes_, 0U, false};
}

StorageStep SnapshotStorage::step(std::uint16_t word_budget) noexcept
{
    StorageStep progress{state_, 0U, false};
    while (progress.words < word_budget && busy(state_)) {
        ++progress.words;
        std::uint8_t word[2]{};
        switch (phase_) {
        case Phase::ScanHeader:
            if (!read_word(offset_, word)) { break; }
            header_[offset_] = word[0]; header_[offset_ + 1U] = word[1]; offset_ = static_cast<std::uint16_t>(offset_ + 2U);
            if (offset_ == StorageHeaderBytes) { inspect_header(); }
            break;
        case Phase::ScanBlank:
            if (!read_word(offset_, word)) { break; }
            if (binary::read_u16_le(word) != 0xffffU) { next_scan_bank(false); break; }
            offset_ = static_cast<std::uint16_t>(offset_ + 2U);
            if (offset_ == StorageBankBytes) { next_scan_bank(false, true); }
            break;
        case Phase::ScanPayload:
        case Phase::VerifyPayload:
        case Phase::VerifyCommitPayload: {
            if (!read_word(StorageHeaderBytes + offset_, word)) { break; }
            const auto count = static_cast<std::uint16_t>(std::uint32_t{record_.bytes} - offset_ < 2U ? 1U : 2U);
            if (count == 1U && word[1] != 0xffU) {
                if (phase_ == Phase::ScanPayload) { next_scan_bank(false); } else { fail(StorageError::VerificationFailure); }
                break;
            }
            for (std::uint16_t i{}; i < count; ++i) {
                crc_ = checksum::crc32_ieee_update_byte(crc_, word[i]);
            }
            offset_ = static_cast<std::uint16_t>(offset_ + count);
            if (offset_ == record_.bytes) {
                const auto valid = checksum::crc32_ieee_finalize(crc_) == record_.crc;
                if (phase_ == Phase::ScanPayload) { next_scan_bank(valid); }
                else if (!valid) { fail(StorageError::VerificationFailure); }
                else if (phase_ == Phase::VerifyPayload) { phase_ = Phase::Commit; }
                else {
                    record_.valid = true; mounted_ = true;
                    damaged_ = 0U; unsupported_ = 0U;
                    state_ = StorageState::Complete; result_ = StorageResult::Saved; error_ = StorageError::None;
                }
            }
            break;
        }
        case Phase::StagePayload: {
            const auto count = static_cast<std::uint16_t>(std::uint32_t{bytes_} - offset_ < 2U ? 1U : 2U);
            if (!source_.read(source_.context, offset_, staged_payload_.data() + offset_, count)) {
                // No journal state or flash has been changed while staging.
                state_ = StorageState::Fault; error_ = StorageError::SourceFailure; break;
            }
            offset_ = static_cast<std::uint16_t>(offset_ + count);
            if (offset_ == bytes_) {
                record_ = {}; mounted_ = false;
                page_ = 0U; offset_ = 0U; phase_ = Phase::Erase;
            }
            break;
        }
        case Phase::Erase:
            if (!port_.erase_page(port_.context, page_)) { fail(StorageError::EraseFailure); }
            else { phase_ = Phase::VerifyErase; offset_ = static_cast<std::uint16_t>(page_ * StoragePageBytes); }
            progress.erased = true; progress.state = state_; return progress;
        case Phase::VerifyErase:
            if (!read_word(offset_, word)) { break; }
            if (binary::read_u16_le(word) != 0xffffU) { fail(StorageError::VerificationFailure); break; }
            offset_ = static_cast<std::uint16_t>(offset_ + 2U);
            if (offset_ == (std::uint16_t{page_} + 1U) * StoragePageBytes) {
                ++page_;
                if (page_ == StoragePagesPerBank) {
                    phase_ = Phase::WritePayload; offset_ = 0U; crc_ = checksum::Crc32IeeeInitial;
                }
                else { phase_ = Phase::Erase; }
            }
            break;
        case Phase::WritePayload: {
            const auto count = static_cast<std::uint16_t>(std::uint32_t{bytes_} - offset_ < 2U ? 1U : 2U);
            word[0] = staged_payload_[offset_];
            word[1] = count == 2U ? staged_payload_[offset_ + 1U] : 0xffU;
            if (!program_word(StorageHeaderBytes + offset_, binary::read_u16_le(word))) { break; }
            for (std::uint16_t i{}; i < count; ++i) {
                crc_ = checksum::crc32_ieee_update_byte(crc_, word[i]);
            }
            offset_ = static_cast<std::uint16_t>(offset_ + count);
            if (offset_ == bytes_) { make_header(); phase_ = Phase::WriteHeader; offset_ = 0U; }
            break;
        }
        case Phase::WriteHeader:
            if (!program_word(offset_, binary::read_u16_le(header_.data() + offset_))) { break; }
            offset_ = static_cast<std::uint16_t>(offset_ + 2U);
            if (offset_ == storage_header::CommitOffset) { phase_ = Phase::VerifyHeader; offset_ = 0U; }
            break;
        case Phase::VerifyHeader:
        case Phase::VerifyCommitHeader:
            if (!read_word(offset_, word)) { break; }
            if (word[0] != header_[offset_] || word[1] != header_[offset_ + 1U]) { fail(StorageError::VerificationFailure); break; }
            offset_ = static_cast<std::uint16_t>(offset_ + 2U);
            if (offset_ == StorageHeaderBytes) {
                phase_ = phase_ == Phase::VerifyHeader ? Phase::VerifyPayload : Phase::VerifyCommitPayload;
                offset_ = 0U; crc_ = checksum::Crc32IeeeInitial;
            }
            break;
        case Phase::Commit:
            if (!program_word(storage_header::CommitOffset, StorageCommit)) { break; }
            binary::write_u16_le(header_.data() + storage_header::CommitOffset, StorageCommit);
            phase_ = Phase::VerifyCommitHeader; offset_ = 0U;
            break;
        }
    }
    progress.state = state_; return progress;
}

static_assert(sizeof(SnapshotStorage) <= 384U, "Keep the journal's fixed SRAM workspace bounded");
static_assert(StorageBankBytes == std::uint32_t{StoragePageBytes} * StoragePagesPerBank &&
              StorageRegionBytes == std::uint32_t{StorageBankCount} * StorageBankBytes &&
              StorageBankCount == 1U && StoragePagesPerBank == 1U, "Storage must occupy one physical flash page");
static_assert(StorageHeaderBytes == 32U && StoragePayloadMaximum == 240U &&
              StoragePayloadMaximum + StorageHeaderBytes <= StorageBankBytes &&
              StoragePayloadReadMaximum == StoragePayloadWriteMaximum && StorageBankBytes % 2U == 0U,
              "Bounded versioned halfword storage layout");
} // namespace dso
