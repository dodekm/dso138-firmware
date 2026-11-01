#include "dso_scope_storage.hpp"

namespace dso {
namespace {
std::uint16_t little16(const std::uint8_t* bytes) noexcept
{ return static_cast<std::uint16_t>(std::uint16_t{bytes[0]} | static_cast<std::uint16_t>(std::uint16_t{bytes[1]} << 8U)); }
std::uint32_t crc_byte(std::uint32_t crc, std::uint8_t byte) noexcept
{
    crc ^= byte;
    for (unsigned bit{}; bit < 8U; ++bit) { crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xedb88320U : 0U); }
    return crc;
}
bool supported_header(const std::array<std::uint8_t, CalibrationSupportedRecordMaximum>& bytes) noexcept
{
    return bytes[0] == 'S' && bytes[1] == 'C' && bytes[2] == 'P' && bytes[3] == '1' &&
        (little16(bytes.data() + 4U) >= 1U && little16(bytes.data() + 4U) <= 4U) && little16(bytes.data() + 6U) == SavedRecordHeaderBytes;
}
bool supported_calibration(const std::uint8_t* bytes) noexcept
{ return bytes[0] == 'D' && bytes[1] == 'S' && bytes[2] == 'C' && bytes[3] == '1' && (little16(bytes + 4U) == 1U || little16(bytes + 4U) == 2U); }

bool packed_sample_byte(SavedSampleSource source, std::uint16_t sample_count,
                        std::uint16_t byte_index, std::uint8_t& output) noexcept
{
    const auto pair = static_cast<std::uint16_t>(byte_index / 3U);
    const auto lane = static_cast<std::uint16_t>(byte_index % 3U);
    const auto first_index = static_cast<std::uint16_t>(2U * pair);
    std::uint16_t first{};
    if (source.read == nullptr || !source.read(source.context, first_index, first) || first > AdcMax) { return false; }
    if (lane == 0U) { output = static_cast<std::uint8_t>(first); return true; }
    std::uint16_t second{};
    if (first_index + 1U < sample_count &&
        (!source.read(source.context, static_cast<std::uint16_t>(first_index + 1U), second) || second > AdcMax)) { return false; }
    output = lane == 1U ? static_cast<std::uint8_t>(((first >> 8U) & 0x0fU) | ((second & 0x0fU) << 4U)) :
        static_cast<std::uint8_t>(second >> 4U);
    return true;
}

bool read_journal_sample(const SnapshotStorage& journal, const StorageRecord& candidate,
                         const SavedRecord& record, bool packed, std::uint16_t index,
                         std::uint16_t& sample) noexcept
{
    if (index >= record.frame.capture.length) { return false; }
    std::uint8_t bytes[2]{};
    const auto offset = static_cast<std::uint16_t>(SavedRecordHeaderBytes +
        (packed ? 3U * (index / 2U) + (index % 2U) : 2U * index));
    if (!journal.read(candidate, offset, bytes, 2U)) { return false; }
    std::uint16_t value{};
    if (!packed) { value = little16(bytes); }
    else if ((index & 1U) == 0U) {
        if (index + 1U == record.frame.capture.length && (bytes[1] & 0xf0U) != 0U) { sample = UINT16_MAX; return true; }
        value = static_cast<std::uint16_t>(bytes[0] | (static_cast<std::uint16_t>(bytes[1] & 0x0fU) << 8U));
    } else { value = static_cast<std::uint16_t>((bytes[0] >> 4U) | (std::uint16_t{bytes[1]} << 4U)); }
    sample = value;
    return true;
}

struct SelectedByteContext final { const SnapshotStorage* journal; const StorageRecord* selected; };
bool selected_byte_read(void* raw, std::uint16_t offset, std::uint8_t* output, std::uint16_t count) noexcept
{
    const auto& context = *static_cast<SelectedByteContext*>(raw);
    return context.journal->read(*context.selected, offset, output, count);
}
}

ScopeStorage::ScopeStorage(SnapshotStorage& journal, std::uint64_t board_identity, bool writable) noexcept
    : journal_{journal}, board_identity_{board_identity},
      state_{board_identity == 0U ? ScopeStorageState::Disabled : ScopeStorageState::Idle}, writable_{writable}
{}

void ScopeStorage::fail(ScopeStorageError error) noexcept
{
    state_ = ScopeStorageState::Error; error_ = error; samples_ = {}; calibration_ = {};
    // A journal write/read fault requires a fresh scan before any bank can be
    // consumed again, even though the older committed bits are preserved.
    if (!journal_.mounted()) { published_ = false; }
}

bool ScopeStorage::begin_open() noexcept
{
    if (state_ == ScopeStorageState::Disabled) { error_ = ScopeStorageError::Disabled; return false; }
    if (state_ == ScopeStorageState::Opening || state_ == ScopeStorageState::Saving) { return false; }
    published_ = false; unsupported_ = false; result_ = ScopeStorageResult::None;
    error_ = ScopeStorageError::None; samples_ = {}; calibration_ = {}; candidate_bank_ = 0U; chosen_bank_ = StorageBankCount;
    if (!journal_.begin_scan()) { fail(ScopeStorageError::Journal); return false; }
    phase_ = Phase::Scan; state_ = ScopeStorageState::Opening; return true;
}

bool ScopeStorage::begin_save(const SavedRecord& record, SavedSampleSource samples, SavedByteSource calibration) noexcept
{
    if (state_ == ScopeStorageState::Disabled) { error_ = ScopeStorageError::Disabled; return false; }
    if (state_ != ScopeStorageState::Ready) { return false; }
    if (!writable_) { error_ = ScopeStorageError::ReadOnly; return false; }
    if (unsupported_ || journal_.unsupported_banks() != 0U) { error_ = ScopeStorageError::UnsupportedFormat; return false; }
    const auto bytes = saved_record_size(record);
    if (record.board_identity != board_identity_ || bytes == 0U ||
        (record.waveform_present && samples.read == nullptr) || (record.calibration_bytes != 0U && calibration.read == nullptr)) {
        error_ = ScopeStorageError::InvalidRecord; return false;
    }
    // Reuse semantic scratch while stopped. The old selected metadata remains
    // available to a preservation callback; any failed scratch use invalidates
    // its cached header proof and requires a new open before consumption.
    for (std::uint16_t offset{}; offset < record.calibration_bytes;) {
        const auto remaining = static_cast<std::uint16_t>(record.calibration_bytes - offset);
        const auto count = static_cast<std::uint16_t>(remaining > 32U ? 32U : remaining);
        if (!calibration.read(calibration.context, offset, header_.data() + offset, count)) {
            published_ = false; fail(ScopeStorageError::ReadFailure); return false;
        }
        offset = static_cast<std::uint16_t>(offset + count);
    }
    if ((record.calibration_bytes != 0U && !calibration_record_valid(header_.data(), record.calibration_bytes, board_identity_)) ||
        !encode_saved_header(record, header_.data(), SavedRecordHeaderBytes)) {
        published_ = false; fail(ScopeStorageError::InvalidRecord); return false;
    }
    samples_ = samples; calibration_ = calibration;
    if (!journal_.begin_save({this, produce}, bytes)) {
        published_ = false; fail(ScopeStorageError::Journal); return false;
    }
    state_ = ScopeStorageState::Saving; result_ = ScopeStorageResult::None; error_ = ScopeStorageError::None;
    return true;
}

bool ScopeStorage::produce(void* context, std::uint16_t offset, std::uint8_t* output, std::uint16_t count) noexcept
{
    const auto& self = *static_cast<ScopeStorage*>(context);
    const auto bytes = little16(self.header_.data() + 8U);
    if (self.state_ != ScopeStorageState::Saving || offset > bytes || count > bytes - offset ||
        (count != 0U && output == nullptr)) { return false; }
    std::uint16_t done{};
    while (done < count) {
        const auto position = static_cast<std::uint16_t>(offset + done);
        if (position < SavedRecordHeaderBytes) { output[done++] = self.header_[position]; continue; }
        const auto end = static_cast<std::uint16_t>(bytes - little16(self.header_.data() + 100U));
        if (position >= end) {
            return self.calibration_.read != nullptr && self.calibration_.read(self.calibration_.context,
                static_cast<std::uint16_t>(position - end), output + done, static_cast<std::uint16_t>(count - done));
        }
        const auto sample_offset = static_cast<std::uint16_t>(position - SavedRecordHeaderBytes);
        if (little16(self.header_.data() + 4U) == 4U) {
            if (!packed_sample_byte(self.samples_, little16(self.header_.data() + 70U), sample_offset, output[done])) { return false; }
        } else {
            const auto index = static_cast<std::uint16_t>(sample_offset / 2U);
            std::uint16_t sample{};
            if (self.samples_.read == nullptr || !self.samples_.read(self.samples_.context, index, sample) || sample > AdcMax) { return false; }
            output[done] = static_cast<std::uint8_t>((sample_offset & 1U) == 0U ? sample : sample >> 8U);
        }
        ++done;
    }
    return true;
}

void ScopeStorage::candidate_valid() noexcept
{
    const auto* candidate = journal_.candidate(candidate_bank_);
    if (candidate != nullptr) { chosen_bank_ = candidate_bank_; }
    next_candidate();
}

void ScopeStorage::samples_valid() noexcept
{
    if (record_.calibration_bytes == 0U) { candidate_valid(); }
    else { phase_ = Phase::CandidateCalibration; offset_ = 0U; }
}

void ScopeStorage::inspect_header(const StorageRecord& candidate) noexcept
{
    const bool decoded = decode_saved_header(header_.data(), SavedRecordHeaderBytes, board_identity_, record_) &&
        little16(header_.data() + 8U) == candidate.bytes;
    if (phase_ == Phase::FinalHeader) {
        if (!decoded || !journal_.select(candidate)) { fail(ScopeStorageError::Journal); return; }
        published_ = true; state_ = ScopeStorageState::Ready; result_ = ScopeStorageResult::Opened;
        error_ = unsupported_ ? ScopeStorageError::UnsupportedFormat : ScopeStorageError::None;
    } else if (!decoded) {
        if (!supported_header(header_)) { unsupported_ = true; }
        next_candidate();
    } else if (!record_.waveform_present) { samples_valid(); }
    else { phase_ = Phase::CandidateSamples; offset_ = 0U; }
}

void ScopeStorage::finish_candidates() noexcept
{
    if (chosen_bank_ >= StorageBankCount) {
        state_ = ScopeStorageState::Ready;
        result_ = journal_.result() == StorageResult::Empty ? ScopeStorageResult::Empty : ScopeStorageResult::NoValidRecord;
        error_ = unsupported_ ? ScopeStorageError::UnsupportedFormat : ScopeStorageError::None;
        return;
    }
    phase_ = Phase::FinalHeader; offset_ = 0U;
}

void ScopeStorage::next_candidate() noexcept
{
    ++candidate_bank_; offset_ = 0U; phase_ = Phase::CandidateHeader;
    if (candidate_bank_ >= StorageBankCount) { finish_candidates(); }
}

bool ScopeStorage::read_selected_sample(std::uint16_t index, std::uint16_t& sample) const noexcept
{
    const auto* selected = journal_.selected();
    if (!published_ || !record_.waveform_present || selected == nullptr || index >= record_.frame.capture.length) { return false; }
    SelectedByteContext context{&journal_, selected};
    return read_saved_sample(record_, {&context, selected_byte_read}, index, sample);
}

bool ScopeStorage::read_selected_calibration(std::uint16_t offset, std::uint8_t* output, std::uint16_t count) const noexcept
{
    const auto* selected = journal_.selected();
    return published_ && selected != nullptr && offset <= record_.calibration_bytes && count <= record_.calibration_bytes - offset &&
        (count == 0U || (output != nullptr && selected->bytes >= record_.calibration_bytes && journal_.read(*selected,
            static_cast<std::uint16_t>(selected->bytes - record_.calibration_bytes + offset), output, count)));
}

bool ScopeStorage::selected_samples_crc_seed(std::uint32_t& seed, std::uint32_t& expected) const noexcept
{
    const auto* selected = journal_.selected();
    if (state_ != ScopeStorageState::Ready || !published_ || selected == nullptr ||
        error_ == ScopeStorageError::Journal || selected->bytes != little16(header_.data() + 8U)) { return false; }
    std::uint32_t crc{0xffffffffU};
    for (std::uint16_t i{}; i < SavedRecordHeaderBytes; ++i) { crc = crc_byte(crc, header_[i]); }
    seed = crc; expected = selected->crc; return true;
}

std::uint32_t ScopeStorage::sample_crc(std::uint32_t seed, std::uint16_t sample) noexcept
{ return crc_byte(crc_byte(seed, static_cast<std::uint8_t>(sample)), static_cast<std::uint8_t>(sample >> 8U)); }

std::uint32_t ScopeStorage::packed_sample_crc12(std::uint32_t seed, std::uint16_t first,
                                               std::uint16_t second, bool has_second) noexcept
{
    seed = crc_byte(seed, static_cast<std::uint8_t>(first));
    const auto middle = static_cast<std::uint8_t>(((first >> 8U) & 0x0fU) |
        (has_second ? ((second & 0x0fU) << 4U) : 0U));
    seed = crc_byte(seed, middle);
    return has_second ? crc_byte(seed, static_cast<std::uint8_t>(second >> 4U)) : seed;
}

bool ScopeStorage::selected_samples_packed() const noexcept
{ return published_ && record_.waveform_present && record_.encoding == SavedSampleEncoding::Packed12; }

ScopeStorageStep ScopeStorage::step(std::uint16_t word_budget) noexcept
{
    ScopeStorageStep progress{state_, 0U, false};
    if (word_budget == 0U) { return progress; }
    if (state_ == ScopeStorageState::Saving) {
        const auto step = journal_.step(word_budget); progress.words = step.words; progress.erased = step.erased;
        if (step.state == StorageState::Fault) { fail(ScopeStorageError::Journal); }
        else if (step.state == StorageState::Complete) {
            samples_ = {}; calibration_ = {};
            if (journal_.result() != StorageResult::Saved || !decode_saved_header(header_.data(), SavedRecordHeaderBytes, board_identity_, record_)) {
                published_ = false; fail(ScopeStorageError::Journal);
            } else { published_ = true; state_ = ScopeStorageState::Ready; result_ = ScopeStorageResult::Saved; }
        }
        progress.state = state_; return progress;
    }
    if (state_ != ScopeStorageState::Opening) { return progress; }
    if (phase_ == Phase::Scan) {
        const auto step = journal_.step(word_budget); progress.words = step.words; progress.erased = step.erased;
        if (step.state == StorageState::Fault) { fail(ScopeStorageError::Journal); }
        else if (step.state == StorageState::Complete) {
            unsupported_ = journal_.unsupported_banks() != 0U;
            phase_ = Phase::CandidateHeader; offset_ = 0U;
        }
    }
    while (state_ == ScopeStorageState::Opening && phase_ != Phase::Scan && progress.words < word_budget) {
        const auto bank = phase_ == Phase::FinalHeader ? chosen_bank_ : candidate_bank_;
        const auto* candidate = journal_.candidate(bank);
        if (candidate == nullptr) {
            if (phase_ == Phase::FinalHeader) { fail(ScopeStorageError::Journal); break; }
            next_candidate(); continue;
        }
        if (phase_ == Phase::CandidatePrefix) {
            const auto remaining = static_cast<std::uint16_t>(6U - offset_);
            const auto available = static_cast<std::uint16_t>(word_budget - progress.words);
            const auto count = static_cast<std::uint16_t>(available < 3U && available * 2U < remaining ? available * 2U : remaining);
            progress.words = static_cast<std::uint16_t>(progress.words + count / 2U);
            const auto tail = static_cast<std::uint16_t>(candidate->bytes - little16(header_.data() + 100U));
            if (!journal_.read(*candidate, static_cast<std::uint16_t>(tail + offset_), header_.data() + SavedRecordHeaderBytes + offset_, count)) {
                fail(ScopeStorageError::ReadFailure); break;
            }
            offset_ = static_cast<std::uint16_t>(offset_ + count);
            if (offset_ != 6U) { continue; }
            // Classify the nested format before current metadata/sample/length
            // semantics can discard it. Future bank formats remain protected
            // even when a waveform sample is bad or the future extent differs.
            if (!supported_calibration(header_.data() + SavedRecordHeaderBytes)) { unsupported_ = true; next_candidate(); }
            else { inspect_header(*candidate); }
            continue;
        }
        if (phase_ == Phase::CandidateSamples) {
            std::uint16_t sample{}; ++progress.words;
            if (!read_journal_sample(journal_, *candidate, record_, little16(header_.data() + 4U) == 4U, offset_, sample)) {
                fail(ScopeStorageError::ReadFailure); break;
            }
            if (sample > AdcMax) { next_candidate(); continue; }
            ++offset_;
            if (offset_ == record_.frame.capture.length) { samples_valid(); }
            continue;
        }
        if (phase_ == Phase::CandidateCalibration) {
            const auto remaining = static_cast<std::uint16_t>(record_.calibration_bytes - offset_);
            const auto words = static_cast<std::uint16_t>(word_budget - progress.words);
            const auto chunk = static_cast<std::uint16_t>(words > 16U ? 32U : words * 2U);
            const auto count = static_cast<std::uint16_t>(chunk > remaining ? remaining : chunk);
            progress.words = static_cast<std::uint16_t>(progress.words + count / 2U);
            const auto tail = static_cast<std::uint16_t>(candidate->bytes - record_.calibration_bytes);
            if (!journal_.read(*candidate, static_cast<std::uint16_t>(tail + offset_), header_.data() + offset_, count)) {
                fail(ScopeStorageError::ReadFailure); break;
            }
            offset_ = static_cast<std::uint16_t>(offset_ + count);
            if (offset_ != record_.calibration_bytes) { continue; }
            if (!supported_calibration(header_.data())) { unsupported_ = true; next_candidate(); }
            else if (!calibration_record_valid(header_.data(), record_.calibration_bytes, board_identity_)) { next_candidate(); }
            else { candidate_valid(); }
            continue;
        }
        // Truncated known-schema records are semantic failures. Still inspect
        // the available format prefix so a future compact schema is protected
        // from writes rather than silently treated as a damaged current one.
        const auto required = candidate->bytes < SavedRecordHeaderBytes ?
            static_cast<std::uint16_t>(candidate->bytes < 8U ? candidate->bytes : 8U) : SavedRecordHeaderBytes;
        const auto remaining = static_cast<std::uint16_t>(required - offset_);
        const auto available_words = static_cast<std::uint16_t>(word_budget - progress.words);
        const auto chunk_words = static_cast<std::uint16_t>(available_words > 16U ? 16U : available_words);
        const auto count = static_cast<std::uint16_t>(2U * chunk_words < remaining ? 2U * chunk_words : remaining);
        progress.words = static_cast<std::uint16_t>(progress.words + (count + 1U) / 2U);
        if (!journal_.read(*candidate, offset_, header_.data() + offset_, count)) { fail(ScopeStorageError::ReadFailure); break; }
        offset_ = static_cast<std::uint16_t>(offset_ + count);
        if (offset_ != required) { continue; }
        if (required < SavedRecordHeaderBytes) {
            if (phase_ == Phase::FinalHeader) { fail(ScopeStorageError::Journal); break; }
            if (required < 8U || !supported_header(header_)) { unsupported_ = true; }
            next_candidate(); continue;
        }
        if (phase_ != Phase::FinalHeader && supported_header(header_) && little16(header_.data() + 4U) >= 2U) {
            const auto length = little16(header_.data() + 100U);
            if (length != 0U) {
                if (length < 6U || length > candidate->bytes || candidate->bytes - length < SavedRecordHeaderBytes) {
                    unsupported_ = true; next_candidate();
                } else { phase_ = Phase::CandidatePrefix; offset_ = 0U; }
                continue;
            }
        }
        inspect_header(*candidate);
    }
    progress.state = state_; return progress;
}
} // namespace dso
static_assert(dso::SavedRecordCalibrationMaximumBytes == dso::CalibrationSupportedRecordMaximum,
              "Codec and bank supported extents must agree");
