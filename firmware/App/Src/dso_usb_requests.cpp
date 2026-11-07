#include "dso_usb_service.hpp"
#include "dso_binary_endian.hpp"
#include "dso_adc_contract.hpp"
#include "dso_instrument_codes.hpp"
#include "dso_timebase_contract.hpp"
#include "dso_usb_protocol_contract.hpp"
#include "dso_ui_controller.hpp"
#include "dso_capture_config.hpp"
#include "dso_physical_settings.hpp"
#include "dso_scaling.hpp"
#include "dso_trigger.hpp"
#include "dso_voltage_calibration.hpp"

#include <array>
#include <cstring>

namespace dso {
namespace {
namespace metadata_layout = usb_protocol_contract::frame_metadata_body;
namespace info_layout = usb_protocol_contract::info;
namespace read_sample_layout = usb_protocol_contract::read_samples;
namespace settings_layout = usb_protocol_contract::settings_body;
namespace capture_job_layout = usb_protocol_contract::capture_job;
namespace status_body_layout = usb_protocol_contract::status_body;
namespace status_request_layout = usb_protocol_contract::status_request;
namespace settings_request_layout = usb_protocol_contract::settings_request;

constexpr UsbStatus settings_wire_status(HostSettingsResult result) noexcept
{
    switch (result) {
    case HostSettingsResult::Applied: return UsbStatus::Ok;
    case HostSettingsResult::Invalid: return UsbStatus::Invalid;
    case HostSettingsResult::Busy: return UsbStatus::Busy;
    case HostSettingsResult::Stale: return UsbStatus::Stale;
    }
    return UsbStatus::Fault;
}

static_assert(settings_wire_status(HostSettingsResult::Applied) == UsbStatus::Ok &&
                  settings_wire_status(HostSettingsResult::Invalid) == UsbStatus::Invalid &&
                  settings_wire_status(HostSettingsResult::Busy) == UsbStatus::Busy &&
                  settings_wire_status(HostSettingsResult::Stale) == UsbStatus::Stale,
              "Controller settings results must retain their protocol-v1 statuses");

template <std::size_t Count>
constexpr bool codes_match(const std::array<std::uint8_t, Count>& firmware,
                           const std::array<std::uint8_t, Count>& wire) noexcept
{
    for (std::size_t i{}; i < Count; ++i) {
        if (firmware[i] != wire[i]) { return false; }
    }
    return true;
}

static_assert(codes_match(std::array<std::uint8_t, 3U>{
                              static_cast<std::uint8_t>(TriggerMode::Normal),
                              static_cast<std::uint8_t>(TriggerMode::Auto),
                              static_cast<std::uint8_t>(TriggerMode::Single)},
                          std::array<std::uint8_t, 3U>{
                              static_cast<std::uint8_t>(instrument_code_contract::TriggerMode::Normal),
                              static_cast<std::uint8_t>(instrument_code_contract::TriggerMode::Auto),
                              static_cast<std::uint8_t>(instrument_code_contract::TriggerMode::Single)}),
              "Firmware trigger modes must keep their established serialized codes");
static_assert(codes_match(std::array<std::uint8_t, 2U>{
                              static_cast<std::uint8_t>(TriggerEdge::Rising),
                              static_cast<std::uint8_t>(TriggerEdge::Falling)},
                          std::array<std::uint8_t, 2U>{
                              static_cast<std::uint8_t>(instrument_code_contract::TriggerEdge::Rising),
                              static_cast<std::uint8_t>(instrument_code_contract::TriggerEdge::Falling)}),
              "Firmware trigger edges must keep their established serialized codes");
static_assert(codes_match(std::array<std::uint8_t, 4U>{
                              static_cast<std::uint8_t>(InputCoupling::Unknown),
                              static_cast<std::uint8_t>(InputCoupling::Dc),
                              static_cast<std::uint8_t>(InputCoupling::Ac),
                              static_cast<std::uint8_t>(InputCoupling::Ground)},
                          std::array<std::uint8_t, 4U>{
                              static_cast<std::uint8_t>(instrument_code_contract::InputCoupling::Unknown),
                              static_cast<std::uint8_t>(instrument_code_contract::InputCoupling::Dc),
                              static_cast<std::uint8_t>(instrument_code_contract::InputCoupling::Ac),
                              static_cast<std::uint8_t>(instrument_code_contract::InputCoupling::Ground)}),
              "Firmware coupling values must keep their established serialized codes");
static_assert(codes_match(std::array<std::uint8_t, 3U>{
                              static_cast<std::uint8_t>(CalibrationKind::Unavailable),
                              static_cast<std::uint8_t>(CalibrationKind::Nominal),
                              static_cast<std::uint8_t>(CalibrationKind::Measured)},
                          std::array<std::uint8_t, 3U>{
                              static_cast<std::uint8_t>(instrument_code_contract::CalibrationKind::Unavailable),
                              static_cast<std::uint8_t>(instrument_code_contract::CalibrationKind::Nominal),
                              static_cast<std::uint8_t>(instrument_code_contract::CalibrationKind::Measured)}),
              "Firmware calibration kinds must keep their established serialized codes");
static_assert(codes_match(std::array<std::uint8_t, 7U>{
                              static_cast<std::uint8_t>(UiRunState::Hold),
                              static_cast<std::uint8_t>(UiRunState::WaitingForControls),
                              static_cast<std::uint8_t>(UiRunState::WaitingForBackend),
                              static_cast<std::uint8_t>(UiRunState::ArmPending),
                              static_cast<std::uint8_t>(UiRunState::Acquiring),
                              static_cast<std::uint8_t>(UiRunState::FrameReady),
                              static_cast<std::uint8_t>(UiRunState::Fault)},
                          std::array<std::uint8_t, 7U>{
                              static_cast<std::uint8_t>(instrument_code_contract::UiState::Hold),
                              static_cast<std::uint8_t>(instrument_code_contract::UiState::WaitingForControls),
                              static_cast<std::uint8_t>(instrument_code_contract::UiState::WaitingForBackend),
                              static_cast<std::uint8_t>(instrument_code_contract::UiState::ArmPending),
                              static_cast<std::uint8_t>(instrument_code_contract::UiState::Acquiring),
                              static_cast<std::uint8_t>(instrument_code_contract::UiState::FrameReady),
                              static_cast<std::uint8_t>(instrument_code_contract::UiState::Fault)}),
              "Firmware UI states must keep their established serialized codes");

#if defined(__GNUC__) && defined(__ARM_FEATURE_UNALIGNED) && defined(__BYTE_ORDER__) && \
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
// Copy the frozen sample bytes directly on the little-endian STM32 target.
std::uint16_t get16(const std::uint8_t* p) noexcept
{ std::uint16_t value; __builtin_memcpy(&value, p, sizeof(value)); return value; }
std::uint32_t get32(const std::uint8_t* p) noexcept
{ std::uint32_t value; __builtin_memcpy(&value, p, sizeof(value)); return value; }
void put16(std::uint8_t* p, std::uint16_t value) noexcept
{ __builtin_memcpy(p, &value, sizeof(value)); }
void put32(std::uint8_t* p, std::uint32_t value) noexcept
{ __builtin_memcpy(p, &value, sizeof(value)); }
void put_samples(std::uint8_t* out, const std::uint16_t* samples, std::uint8_t count) noexcept
{ std::memcpy(out, samples, std::size_t{count} * sizeof(*samples)); }
#else
std::uint16_t get16(const std::uint8_t* p) noexcept
{ return binary::read_u16_le(p); }
std::uint32_t get32(const std::uint8_t* p) noexcept
{ return binary::read_u32_le(p); }
void put16(std::uint8_t* p, std::uint16_t value) noexcept
{ binary::write_u16_le(p, value); }
void put32(std::uint8_t* p, std::uint32_t value) noexcept
{ binary::write_u32_le(p, value); }
void put_samples(std::uint8_t* out, const std::uint16_t* samples, std::uint8_t count) noexcept
{
    for (std::uint8_t i{}; i < count; ++i) {
        put16(out + std::size_t{i} * sizeof(*samples), samples[i]);
    }
}
#endif
void shift_rx(std::uint8_t* bytes, std::uint16_t& size, std::uint16_t count) noexcept
{
    if (count >= size) { size = 0U; return; }
    size = static_cast<std::uint16_t>(size - count);
    std::memmove(bytes, bytes + count, size);
}
}

void UsbProtocolService::respond(std::uint8_t opcode, std::uint16_t request_id,
                                 UsbStatus status, std::uint8_t payload_size) noexcept
{
    if (payload_size > usb_wire::MaximumResponseBody) { return; }
    // Valid requests encode directly in their replay buffer. Parser errors
    // and conflicting IDs use scratch so the last valid response survives.
    auto* frame = response_buffer();
    frame[usb_wire::MagicFirstOffset] = usb_wire::MagicFirst;
    frame[usb_wire::MagicSecondOffset] = usb_wire::MagicSecond;
    frame[usb_wire::VersionOffset] = UsbProtocolVersion;
    frame[usb_wire::OpcodeOffset] = static_cast<std::uint8_t>(opcode | 0x80U);
    put16(frame + usb_wire::RequestIdOffset, request_id);
    const auto total_payload = static_cast<std::uint8_t>(payload_size + usb_wire::StatusBytes);
    put16(frame + usb_wire::PayloadSizeOffset, total_payload);
    frame[usb_wire::PayloadOffset] = static_cast<std::uint8_t>(status);
    put32(frame + usb_wire::CrcOffset,
          usb_protocol_crc(frame, frame + usb_wire::PayloadOffset, total_payload));
    tx_size_ = static_cast<std::uint8_t>(usb_wire::HeaderBytes + total_payload);
    tx_pending_ = frame;
    if (cache_response_) { replay_size_ = tx_size_; }
}

void UsbProtocolService::respond_settings(std::uint8_t opcode, std::uint16_t request_id) noexcept
{
    auto* out = response_buffer() + usb_wire::ResponseBodyOffset;
    const auto& ui = controller_.ui();
    const auto& settings = ui.settings();
    const auto& physical = ui.physical();
    const auto* timing = controller_.selected_timing();
    out[settings_layout::SchemaOffset] = 1U; // Settings schema
    out[settings_layout::WritableMaskOffset] = usb_protocol_contract::settings_writable::SupportedMask;
    out[settings_layout::UiStateOffset] = static_cast<std::uint8_t>(ui.state());
    out[settings_layout::FlagsOffset] = static_cast<std::uint8_t>(
        usb_protocol_contract::settings_flags::ExtendedCapture |
        (ui.wants_run() ? usb_protocol_contract::settings_flags::RunIntent : 0U) |
        (ui.physical_ready() ? usb_protocol_contract::settings_flags::PhysicalSettingsQualified : 0U) |
        (controller_.host_settings_writable()
             ? usb_protocol_contract::settings_flags::WriteAdmissibleNow
             : 0U) |
        (controller_.host_hold_available()
             ? usb_protocol_contract::settings_flags::LocalRunPauseAdmissibleNow
             : 0U));
    out[settings_layout::TimebaseIndexOffset] = settings.timebase_index;
    out[settings_layout::CatalogueCountOffset] = timebase_contract::ClassicTimebaseCatalogueEntries;
    out[settings_layout::TriggerModeOffset] = static_cast<std::uint8_t>(settings.mode);
    out[settings_layout::TriggerEdgeOffset] = static_cast<std::uint8_t>(settings.edge);
    put16(out + settings_layout::TriggerLevelOffset, settings.trigger_level_code);
    put16(out + settings_layout::TriggerPositionOffset, settings.trigger_position_permille);
    put32(out + settings_layout::MicrovoltsPerDivisionOffset, physical.microvolts_per_div);
    put32(out + settings_layout::TimerHzOffset, timing == nullptr ? 0U : timing->timer_hz);
    put32(out + settings_layout::TimerDividerOffset, timing == nullptr ? 0U : timing->divider);
    out[settings_layout::CouplingOffset] = static_cast<std::uint8_t>(physical.coupling);
    out[settings_layout::CatalogueIdOffset] = timebase_contract::ClassicCatalogueId;
    put16(out + settings_layout::RevisionOffset, controller_.host_settings_revision());
    put32(out + settings_layout::TimebaseMicrosecondsPerDivisionOffset,
          scope_timebase_us(settings.timebase_index));
    respond(opcode, request_id, UsbStatus::Ok, settings_layout::BodyBytes);
}

bool UsbProtocolService::process_one(std::uint32_t now_ms) noexcept
{
    std::uint16_t magic_offset{};
    while (magic_offset + usb_wire::MagicBytes <= last_rx_size_ &&
           (rx_[magic_offset + usb_wire::MagicFirstOffset] != usb_wire::MagicFirst ||
            rx_[magic_offset + usb_wire::MagicSecondOffset] != usb_wire::MagicSecond)) {
        ++magic_offset;
    }
    if (magic_offset != 0U) { shift_rx(rx_.data(), last_rx_size_, magic_offset); }
    if (last_rx_size_ >= usb_wire::MagicBytes &&
        (rx_[usb_wire::MagicFirstOffset] != usb_wire::MagicFirst ||
         rx_[usb_wire::MagicSecondOffset] != usb_wire::MagicSecond)) {
        shift_rx(rx_.data(), last_rx_size_, static_cast<std::uint16_t>(last_rx_size_ - 1U));
    }
    if (last_rx_size_ < usb_wire::HeaderBytes) { return false; }
    const auto request_id = get16(rx_.data() + usb_wire::RequestIdOffset);
    const auto opcode = rx_[usb_wire::OpcodeOffset];
    const auto payload_size = get16(rx_.data() + usb_wire::PayloadSizeOffset);
    if (rx_[usb_wire::VersionOffset] != UsbProtocolVersion || request_id == 0U ||
        payload_size > UsbProtocolMaximumPayload) {
        shift_rx(rx_.data(), last_rx_size_, 1U);
        if (request_id != 0U) { cache_response_ = false; respond(opcode, request_id, UsbStatus::Invalid); }
        return tx_pending_ != nullptr;
    }
    const auto frame_size = static_cast<std::uint16_t>(usb_wire::HeaderBytes + payload_size);
    if (last_rx_size_ < frame_size) { return false; }
    const auto request_crc = usb_protocol_crc(rx_.data(), rx_.data() + usb_wire::PayloadOffset, payload_size);
    if (request_crc != get32(rx_.data() + usb_wire::CrcOffset)) {
        shift_rx(rx_.data(), last_rx_size_, 1U);
        cache_response_ = false;
        respond(opcode, request_id, UsbStatus::Invalid);
        return true;
    }
    last_activity_ms_ = now_ms;
    if (request_id == last_request_id_) {
        cache_response_ = false;
        if (opcode == last_opcode_ && request_crc == last_request_crc_ && replay_size_ != 0U) {
            tx_pending_ = replay_.data();
            tx_size_ = replay_size_;
        } else {
            respond(opcode, request_id, UsbStatus::Invalid);
        }
        shift_rx(rx_.data(), last_rx_size_, frame_size);
        return true;
    }
    // A CRC-valid dispatch always replies, so these fields can serve as both
    // the in-progress request key and the replay key.
    last_request_id_ = request_id;
    last_opcode_ = opcode;
    last_request_crc_ = request_crc;
    cache_response_ = true;
    dispatch(now_ms, opcode, request_id, rx_.data() + usb_wire::PayloadOffset, payload_size);
    shift_rx(rx_.data(), last_rx_size_, frame_size);
    return true;
}

void UsbProtocolService::dispatch(std::uint32_t, std::uint8_t opcode,
                                  std::uint16_t request_id,
                                  const std::uint8_t* payload,
                                  std::uint16_t payload_size) noexcept
{
    // Build response payload in place; respond() fills status/header/CRC.
    auto* out = response_buffer() + usb_wire::ResponseBodyOffset;
    switch (static_cast<UsbOpcode>(opcode)) {
    case UsbOpcode::Info:
        if (payload_size != 0U) { respond(opcode, request_id, UsbStatus::Invalid); return; }
        out[info_layout::ProtocolVersionOffset] = UsbProtocolVersion;
        put16(out + info_layout::CaptureCapacityOffset, CaptureCapacity);
        out[info_layout::RawAdcResolutionBitsOffset] = adc_contract::ResolutionBits;
        out[info_layout::MaximumSamplesPerReadOffset] = UsbProtocolMaximumSamplesPerRead;
        out[info_layout::ReservedOffset] = 0U;
        out[info_layout::ReservedOffset + 1U] = 0U; // reserved capability bits
        respond(opcode, request_id, UsbStatus::Ok, info_layout::BodyBytes);
        return;
    case UsbOpcode::Status: {
        if (payload_size != status_request_layout::PayloadBytes) {
            respond(opcode, request_id, UsbStatus::Invalid);
            return;
        }
        const auto requested_job = get32(payload + status_request_layout::JobIdOffset);
        if (requested_job != 0U && requested_job != host_job_) { respond(opcode, request_id, UsbStatus::Stale); return; }
        out[status_body_layout::UiStateOffset] = static_cast<std::uint8_t>(controller_.ui().state());
        out[status_body_layout::ControllerFaultOffset] = static_cast<std::uint8_t>(controller_.fault());
        out[status_body_layout::WorkOffset] = static_cast<std::uint8_t>(controller_.work());
        out[status_body_layout::HostStateOffset] = static_cast<std::uint8_t>(host_job_ == 0U ? HostCaptureState::None :
            controller_.host_capture_state(host_job_));
        put32(out + status_body_layout::JobIdOffset, host_job_);
        const auto* frame = controller_.frame_metadata();
        out[status_body_layout::FrameAvailableOffset] = frame != nullptr ? 1U : 0U;
        put16(out + status_body_layout::FrameCountOffset, frame == nullptr ? 0U : frame->capture.length);
        out[status_body_layout::StorageBusyOffset] = controller_.storage_busy() ? 1U : 0U;
        const auto status = capture_timed_out_ && requested_job == host_job_ ? UsbStatus::Timeout : UsbStatus::Ok;
        respond(opcode, request_id, status, status_body_layout::BodyBytes);
        return;
    }
    case UsbOpcode::BeginFrame:
    case UsbOpcode::BeginFrameExtended: {
        if (payload_size != 0U) { respond(opcode, request_id, UsbStatus::Invalid); return; }
        const auto token = controller_.begin_export();
        if (token == 0U) { respond(opcode, request_id, UsbStatus::Busy); return; }
        // The record stays frozen across chunks while this export lease is active.
        const auto* frame = controller_.frame_metadata();
        const auto& f = *frame;
        const auto calibration = controller_.frame_calibration();
        put32(out + metadata_layout::TokenOffset, token);
        put16(out + metadata_layout::SampleCountOffset, f.capture.length);
        put16(out + metadata_layout::PretriggerOffset, f.capture.pretrigger);
        put32(out + metadata_layout::TimerHzOffset, f.timing.timer_hz);
        put32(out + metadata_layout::TimerDividerOffset, f.timing.divider);
        put32(out + metadata_layout::AcquisitionEpochOffset, f.acquisition_epoch);
        put32(out + metadata_layout::TriggerSequenceOffset, f.trigger_sequence);
        put32(out + metadata_layout::PhysicalGenerationOffset, f.physical.generation);
        put32(out + metadata_layout::MicrovoltsPerDivisionOffset, f.physical.microvolts_per_div);
        put32(out + metadata_layout::ZeroCodeQ16Offset,
              static_cast<std::uint32_t>(calibration.zero_code_q16));
        put32(out + metadata_layout::GainQ16Offset, calibration.microvolts_per_code_q16);
        out[metadata_layout::GainShiftOffset] = calibration.gain_shift;
        out[metadata_layout::CalibrationKindOffset] = static_cast<std::uint8_t>(calibration.kind);
        out[metadata_layout::CouplingOffset] = static_cast<std::uint8_t>(f.physical.coupling);
        out[metadata_layout::TriggerModeOffset] = static_cast<std::uint8_t>(f.capture.mode);
        out[metadata_layout::TriggerEdgeOffset] = static_cast<std::uint8_t>(f.capture.edge);
        // Bits 0..2 remain reserved for legacy filtering metadata.
        out[metadata_layout::FlagsOffset] = f.edge_triggered ? UsbCaptureFlagEdgeTriggered : 0U;
        out[metadata_layout::ReservedOffset] = 0U;
        if (opcode == static_cast<std::uint8_t>(UsbOpcode::BeginFrameExtended)) {
            out[metadata_layout::ExtendedTimebaseIndexOffset] = controller_.frame_timebase_index();
            put16(out + metadata_layout::ExtendedTriggerLevelOffset, f.capture.level);
            out[metadata_layout::ExtendedReservedOffset] = 0U;
            respond(opcode, request_id, UsbStatus::Ok, metadata_layout::ExtendedBodyBytes);
        } else {
            respond(opcode, request_id, UsbStatus::Ok, metadata_layout::BaseBodyBytes);
        }
        return;
    }
    case UsbOpcode::ReadSamples: {
        if (payload_size != read_sample_layout::RequestBytes) {
            respond(opcode, request_id, UsbStatus::Invalid); return;
        }
        const auto token = get32(payload + read_sample_layout::RequestTokenOffset);
        const auto offset = get16(payload + read_sample_layout::RequestSampleOffset);
        const auto count = payload[read_sample_layout::RequestCountOffset];
        const auto* samples = controller_.export_samples(token);
        if (samples == nullptr) {
            respond(opcode, request_id, UsbStatus::Stale); return;
        }
        const auto* frame = controller_.frame_metadata();
        const auto length = frame->capture.length;
        if (count == 0U || count > UsbProtocolMaximumSamplesPerRead ||
            offset > length || count > static_cast<std::uint16_t>(length - offset)) {
            respond(opcode, request_id, UsbStatus::Invalid); return;
        }
        put32(out + read_sample_layout::ResponseTokenOffset, token);
        put16(out + read_sample_layout::ResponseSampleOffset, offset);
        out[read_sample_layout::ResponseCountOffset] = count;
        put_samples(out + read_sample_layout::ResponseSamplesOffset, samples + offset, count);
        respond(opcode, request_id, UsbStatus::Ok,
                static_cast<std::uint8_t>(read_sample_layout::ResponsePrefixBytes +
                                          static_cast<std::uint16_t>(count) *
                                              read_sample_layout::SampleBytes));
        return;
    }
    case UsbOpcode::EndFrame: {
        if (payload_size != 4U) { respond(opcode, request_id, UsbStatus::Invalid); return; }
        const auto token = get32(payload);
        if (!controller_.end_export(token)) { respond(opcode, request_id, UsbStatus::Stale); return; }
        respond(opcode, request_id, UsbStatus::Ok);
        return;
    }
    case UsbOpcode::CaptureSingle:
    case UsbOpcode::CaptureSelected: {
        if (payload_size != capture_job_layout::RequestPayloadBytes) {
            respond(opcode, request_id, UsbStatus::Invalid);
            return;
        }
        const auto timeout = get32(payload + capture_job_layout::TimeoutMillisecondsOffset);
        if (timeout == 0U || timeout > 600000U) { respond(opcode, request_id, UsbStatus::Invalid); return; }
        if (host_job_ != 0U && controller_.host_capture_state(host_job_) == HostCaptureState::Pending) {
            respond(opcode, request_id, UsbStatus::Busy); return;
        }
        const auto job = controller_.request_host_single(opcode == static_cast<std::uint8_t>(UsbOpcode::CaptureSelected));
        if (job == 0U) { respond(opcode, request_id, UsbStatus::Busy); return; }
        host_job_ = job;
        capture_deadline_ms_ = last_activity_ms_ + timeout;
        capture_timed_out_ = false;
        put32(out + capture_job_layout::JobIdOffset, job);
        respond(opcode, request_id, UsbStatus::Ok, capture_job_layout::ResponseBodyBytes);
        return;
    }
    case UsbOpcode::CancelCapture: {
        if (payload_size != 4U) { respond(opcode, request_id, UsbStatus::Invalid); return; }
        const auto job = get32(payload);
        if (job == 0U || job != host_job_) { respond(opcode, request_id, UsbStatus::Stale); return; }
        if (capture_timed_out_) { respond(opcode, request_id, UsbStatus::Timeout); return; }
        if (!controller_.cancel_host_capture(job)) { respond(opcode, request_id, UsbStatus::Stale); return; }
        respond(opcode, request_id, UsbStatus::Cancelled);
        return;
    }
    case UsbOpcode::GetSettings:
        if (payload_size != 0U) { respond(opcode, request_id, UsbStatus::Invalid); return; }
        respond_settings(opcode, request_id);
        return;
    case UsbOpcode::SetSettings:
    case UsbOpcode::SetTrigger:
    case UsbOpcode::HoldLocalRun: {
        const bool trigger = opcode == static_cast<std::uint8_t>(UsbOpcode::SetTrigger);
        const bool hold = opcode == static_cast<std::uint8_t>(UsbOpcode::HoldLocalRun);
        const auto required_size = hold ? settings_request_layout::hold_local_run::PayloadBytes :
            trigger ? settings_request_layout::set_trigger::PayloadBytes :
                      settings_request_layout::set_settings::PayloadBytes;
        if (payload_size != required_size) {
            respond(opcode, request_id, UsbStatus::Invalid); return;
        }
        HostSettingsResult result;
        const auto revision = get16(payload + settings_request_layout::RevisionOffset);
        if (hold) {
            result = controller_.hold_from_host(revision);
        } else {
            auto settings = controller_.ui().settings();
            if (trigger) {
                settings.edge = static_cast<TriggerEdge>(
                    payload[settings_request_layout::set_trigger::EdgeOffset]);
                settings.trigger_level_code = get16(payload + settings_request_layout::set_trigger::LevelOffset);
                settings.trigger_position_permille =
                    get16(payload + settings_request_layout::set_trigger::PositionOffset);
            } else {
                settings.timebase_index =
                    payload[settings_request_layout::set_settings::TimebaseIndexOffset];
                settings.mode = static_cast<TriggerMode>(
                    payload[settings_request_layout::set_settings::TriggerModeOffset]);
            }
            result = controller_.apply_host_settings(revision, settings);
        }
        if (result == HostSettingsResult::Applied) { respond_settings(opcode, request_id); }
        else { respond(opcode, request_id, settings_wire_status(result)); }
        return;
    }
    default:
        respond(opcode, request_id, UsbStatus::Unsupported);
        return;
    }
}

} // namespace dso
