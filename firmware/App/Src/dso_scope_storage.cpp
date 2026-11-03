#include "dso_scope_storage.hpp"

namespace dso {
namespace {
std::uint16_t little16(const std::uint8_t* bytes) noexcept
{ return static_cast<std::uint16_t>(std::uint16_t{bytes[0]} | static_cast<std::uint16_t>(std::uint16_t{bytes[1]} << 8U)); }
}

ScopeStorage::ScopeStorage(SnapshotStorage& journal, std::uint64_t board_identity, bool writable) noexcept
    : journal_{journal}, board_identity_{board_identity},
      state_{board_identity == 0U ? ScopeStorageState::Disabled : ScopeStorageState::Idle}, writable_{writable}
{}

void ScopeStorage::fail(ScopeStorageError error) noexcept
{
    state_ = ScopeStorageState::Error;
    error_ = error;
    if (!journal_.mounted()) { published_ = false; }
}

bool ScopeStorage::begin_open() noexcept
{
    if (state_ == ScopeStorageState::Disabled) { error_ = ScopeStorageError::Disabled; return false; }
    if (state_ == ScopeStorageState::Opening || state_ == ScopeStorageState::Saving) { return false; }
    published_ = false; unsupported_ = false; result_ = ScopeStorageResult::None;
    error_ = ScopeStorageError::None; offset_ = 0U; header_bytes_ = 0U;
    if (!journal_.begin_scan()) { fail(ScopeStorageError::Journal); return false; }
    phase_ = Phase::Scan; state_ = ScopeStorageState::Opening;
    return true;
}

bool ScopeStorage::begin_save(const SavedRecord& record, SavedByteSource calibration) noexcept
{
    if (state_ == ScopeStorageState::Disabled) { error_ = ScopeStorageError::Disabled; return false; }
    if (state_ != ScopeStorageState::Ready) { return false; }
    if (!writable_) { error_ = ScopeStorageError::ReadOnly; return false; }
    const auto bytes = saved_record_size(record);
    if (record.board_identity != board_identity_ || bytes == 0U || bytes > StoragePayloadWriteMaximum ||
        (record.calibration_bytes != 0U && calibration.read == nullptr &&
         (!published_ || record.calibration_bytes != record_.calibration_bytes || journal_.selected() == nullptr))) {
        error_ = ScopeStorageError::InvalidRecord; return false;
    }

    for (std::uint16_t offset{}; offset < record.calibration_bytes;) {
        const auto remaining = static_cast<std::uint16_t>(record.calibration_bytes - offset);
        const auto count = static_cast<std::uint16_t>(remaining > 32U ? 32U : remaining);
        const bool read_ok = calibration.read != nullptr ?
            calibration.read(calibration.context, offset, calibration_.data() + offset, count) :
            read_selected_calibration(offset, calibration_.data() + offset, count);
        if (!read_ok) { error_ = ScopeStorageError::ReadFailure; return false; }
        offset = static_cast<std::uint16_t>(offset + count);
    }
    if (record.calibration_bytes != 0U &&
        !calibration_record_valid(calibration_.data(), record.calibration_bytes, board_identity_)) {
        error_ = ScopeStorageError::InvalidRecord; return false;
    }
    if (!encode_saved_header(record, header_.data(), SavedRecordHeaderBytes)) {
        error_ = ScopeStorageError::InvalidRecord; return false;
    }
    if (!journal_.begin_save({this, produce}, bytes)) { fail(ScopeStorageError::Journal); return false; }
    record_ = record;
    published_ = false;
    state_ = ScopeStorageState::Saving; result_ = ScopeStorageResult::None; error_ = ScopeStorageError::None;
    return true;
}

bool ScopeStorage::produce(void* context, std::uint16_t offset, std::uint8_t* output, std::uint16_t count) noexcept
{
    const auto& self = *static_cast<ScopeStorage*>(context);
    const auto bytes = saved_record_size(self.record_);
    if (self.state_ != ScopeStorageState::Saving || offset > bytes || count > bytes - offset ||
        (count != 0U && output == nullptr)) { return false; }
    for (std::uint16_t i{}; i < count; ++i) {
        const auto position = static_cast<std::uint16_t>(offset + i);
        output[i] = position < SavedRecordHeaderBytes ? self.header_[position] :
            self.calibration_[position - SavedRecordHeaderBytes];
    }
    return true;
}

void ScopeStorage::inspect_header(const StorageRecord& candidate) noexcept
{
    SavedRecord decoded{};
    if (!decode_saved_header(header_.data(), header_bytes_, board_identity_, decoded) ||
        saved_record_size(decoded) != candidate.bytes) {
        if (little16(header_.data() + 4U) != SavedRecordCurrentSchema) { unsupported_ = true; }
        finish_without_record();
        return;
    }
    record_ = decoded;
    if (decoded.calibration_bytes == 0U) { candidate_valid(); }
    else { phase_ = Phase::CandidateCalibration; offset_ = 0U; }
}

void ScopeStorage::candidate_valid() noexcept
{
    const auto* candidate = journal_.candidate(0U);
    if (candidate == nullptr) {
        fail(ScopeStorageError::Journal);
        return;
    }
    published_ = true; state_ = ScopeStorageState::Ready; result_ = ScopeStorageResult::Opened;
    error_ = ScopeStorageError::None;
}

void ScopeStorage::finish_without_record() noexcept
{
    offset_ = 0U; header_bytes_ = 0U;
    state_ = ScopeStorageState::Ready;
    result_ = journal_.result() == StorageResult::Empty ? ScopeStorageResult::Empty : ScopeStorageResult::NoValidRecord;
    error_ = unsupported_ ? ScopeStorageError::UnsupportedFormat : ScopeStorageError::None;
}

bool ScopeStorage::read_selected_calibration(std::uint16_t offset, std::uint8_t* output,
                                             std::uint16_t count) const noexcept
{
    const auto* selected = journal_.selected();
    return published_ && selected != nullptr && offset <= record_.calibration_bytes &&
        count <= record_.calibration_bytes - offset &&
        (count == 0U || (output != nullptr && journal_.read(*selected,
            static_cast<std::uint16_t>(SavedRecordHeaderBytes + offset), output, count)));
}

ScopeStorageStep ScopeStorage::step(std::uint16_t word_budget) noexcept
{
    ScopeStorageStep progress{state_, 0U, false};
    if (word_budget == 0U) { return progress; }
    if (state_ == ScopeStorageState::Saving) {
        const auto step = journal_.step(word_budget); progress.words = step.words; progress.erased = step.erased;
        if (step.state == StorageState::Fault) { fail(ScopeStorageError::Journal); }
        else if (step.state == StorageState::Complete) {
            if (journal_.result() != StorageResult::Saved) { fail(ScopeStorageError::Journal); }
            else {
                unsupported_ = false;
                published_ = true; state_ = ScopeStorageState::Ready; result_ = ScopeStorageResult::Saved;
            }
        }
        progress.state = state_; return progress;
    }
    if (state_ != ScopeStorageState::Opening) { return progress; }
    if (phase_ == Phase::Scan) {
        const auto scan = journal_.step(word_budget); progress.words = scan.words; progress.erased = scan.erased;
        if (scan.state == StorageState::Fault) { fail(ScopeStorageError::Journal); }
        else if (scan.state == StorageState::Complete) {
            unsupported_ = journal_.unsupported_banks() != 0U;
            phase_ = Phase::CandidatePrefix; offset_ = 0U;
        }
    }
    while (state_ == ScopeStorageState::Opening && phase_ != Phase::Scan && progress.words < word_budget) {
        const auto* candidate = journal_.candidate(0U);
        if (candidate == nullptr) {
            finish_without_record(); break;
        }
        if (phase_ == Phase::CandidatePrefix) {
            if (candidate->bytes < 8U) { finish_without_record(); continue; }
            const auto remaining = static_cast<std::uint16_t>(8U - offset_);
            const auto available = static_cast<std::uint16_t>(word_budget - progress.words);
            const auto limit = static_cast<std::uint16_t>(available > 16U ? 32U : available * 2U);
            const auto count = limit < remaining ? limit : remaining;
            if (!journal_.read(*candidate, offset_, header_.data() + offset_, count)) { fail(ScopeStorageError::ReadFailure); break; }
            offset_ = static_cast<std::uint16_t>(offset_ + count);
            progress.words = static_cast<std::uint16_t>(progress.words + (count + 1U) / 2U);
            if (offset_ != 8U) { continue; }
            const auto schema = little16(header_.data() + 4U);
            const auto declared = little16(header_.data() + 6U);
            if (header_[0] != 'S' || header_[1] != 'C' || header_[2] != 'P' || header_[3] != '1') {
                unsupported_ = true; finish_without_record(); continue;
            }
            if (schema == SavedRecordCurrentSchema && declared == SavedRecordHeaderBytes) { header_bytes_ = SavedRecordHeaderBytes; }
            else { unsupported_ = true; finish_without_record(); continue; }
            if (candidate->bytes < header_bytes_) { finish_without_record(); continue; }
            offset_ = 0U; phase_ = Phase::CandidateHeader;
            continue;
        }
        if (phase_ == Phase::CandidateHeader) {
            const auto remaining = static_cast<std::uint16_t>(header_bytes_ - offset_);
            const auto available = static_cast<std::uint16_t>(word_budget - progress.words);
            const auto limit = static_cast<std::uint16_t>(available > 16U ? 32U : available * 2U);
            const auto count = limit < remaining ? limit : remaining;
            if (count == 0U) { break; }
            if (!journal_.read(*candidate, offset_, header_.data() + offset_, count)) { fail(ScopeStorageError::ReadFailure); break; }
            offset_ = static_cast<std::uint16_t>(offset_ + count);
            progress.words = static_cast<std::uint16_t>(progress.words + (count + 1U) / 2U);
            if (offset_ == header_bytes_) { inspect_header(*candidate); }
            continue;
        }
        if (phase_ == Phase::CandidateCalibration) {
            const auto remaining = static_cast<std::uint16_t>(record_.calibration_bytes - offset_);
            const auto available = static_cast<std::uint16_t>(word_budget - progress.words);
            const auto limit = static_cast<std::uint16_t>(available > 16U ? 32U : available * 2U);
            const auto count = limit < remaining ? limit : remaining;
            if (count == 0U) { break; }
            if (!journal_.read(*candidate, static_cast<std::uint16_t>(SavedRecordHeaderBytes + offset_),
                               calibration_.data() + offset_, count)) { fail(ScopeStorageError::ReadFailure); break; }
            offset_ = static_cast<std::uint16_t>(offset_ + count);
            progress.words = static_cast<std::uint16_t>(progress.words + (count + 1U) / 2U);
            if (offset_ == record_.calibration_bytes) {
                if (calibration_record_valid(calibration_.data(), record_.calibration_bytes, board_identity_)) { candidate_valid(); }
                else { finish_without_record(); }
            }
            continue;
        }
    }
    progress.state = state_;
    return progress;
}
} // namespace dso
