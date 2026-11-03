#include "dso_usb_service.hpp"

#include <cstring>

namespace dso {
namespace {
#if defined(__GNUC__) && defined(__ARM_FEATURE_UNALIGNED) && defined(__BYTE_ORDER__) && \
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
// These buffers live in ordinary SRAM. Use alias-safe fixed-size copies so
// the Cortex-M3 compiler can emit a single little-endian load or store.
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
{ return static_cast<std::uint16_t>(std::uint16_t{p[0]} | (std::uint16_t{p[1]} << 8U)); }
std::uint32_t get32(const std::uint8_t* p) noexcept
{
    return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8U) |
        (std::uint32_t{p[2]} << 16U) | (std::uint32_t{p[3]} << 24U);
}
void put16(std::uint8_t* p, std::uint16_t v) noexcept
{ p[0] = static_cast<std::uint8_t>(v); p[1] = static_cast<std::uint8_t>(v >> 8U); }
void put32(std::uint8_t* p, std::uint32_t v) noexcept
{
    p[0] = static_cast<std::uint8_t>(v); p[1] = static_cast<std::uint8_t>(v >> 8U);
    p[2] = static_cast<std::uint8_t>(v >> 16U); p[3] = static_cast<std::uint8_t>(v >> 24U);
}
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
bool deadline_reached(std::uint32_t now, std::uint32_t deadline) noexcept
{ return static_cast<std::int32_t>(now - deadline) >= 0; }
}

UsbProtocolService::UsbProtocolService(ScopeController& controller, UsbStreamPort port) noexcept
    : controller_{controller}, port_{port}
{}

void UsbProtocolService::cancel_owned() noexcept
{
    static_cast<void>(controller_.end_export(controller_.active_export_token()));
    if (host_job_ != 0U && !capture_timed_out_ &&
        controller_.host_capture_state(host_job_) == HostCaptureState::Pending) {
        (void)controller_.cancel_host_capture(host_job_);
    }
    host_job_ = 0U;
    capture_deadline_ms_ = 0U;
    capture_timed_out_ = false;
}

void UsbProtocolService::disconnect() noexcept
{
    cancel_owned();
    controller_.restore_host_settings();
    last_rx_size_ = 0U;
    tx_size_ = replay_size_ = 0U;
    tx_pending_ = nullptr;
    last_request_id_ = 0U;
    last_request_crc_ = 0U;
    last_opcode_ = 0U;
    cache_response_ = false;
}

void UsbProtocolService::service_timeouts(std::uint32_t now_ms) noexcept
{
    if (session_ != 0U && static_cast<std::uint32_t>(now_ms - last_activity_ms_) >= 5000U) {
        // Drop an unsent acknowledgement before rolling its settings back.
        disconnect();
        last_activity_ms_ = now_ms;
    }
}

void UsbProtocolService::service_capture(std::uint32_t now_ms) noexcept
{
    if (host_job_ == 0U || capture_timed_out_) { return; }
    if (deadline_reached(now_ms, capture_deadline_ms_) &&
        controller_.host_capture_state(host_job_) == HostCaptureState::Pending) {
        (void)controller_.cancel_host_capture(host_job_);
        capture_timed_out_ = true;
    }
}

void UsbProtocolService::respond(std::uint8_t opcode, std::uint16_t request_id,
                                 UsbStatus status, std::uint8_t payload_size) noexcept
{
    if (payload_size > 51U) { return; }
    // Valid requests encode directly in their replay buffer. Parser errors
    // and conflicting IDs use scratch so the last valid response survives.
    auto* frame = response_buffer();
    frame[0] = 0x44U; frame[1] = 0x53U; frame[2] = UsbProtocolVersion;
    frame[3] = static_cast<std::uint8_t>(opcode | 0x80U);
    put16(frame + 4U, request_id);
    const auto total_payload = static_cast<std::uint8_t>(payload_size + 1U);
    put16(frame + 6U, total_payload);
    frame[12] = static_cast<std::uint8_t>(status);
    put32(frame + 8U, usb_protocol_crc(frame, frame + 12U, total_payload));
    tx_size_ = static_cast<std::uint8_t>(12U + total_payload);
    tx_pending_ = frame;
    if (cache_response_) { replay_size_ = tx_size_; }
}

void UsbProtocolService::respond_settings(std::uint8_t opcode, std::uint16_t request_id) noexcept
{
    auto* out = response_buffer() + 13U;
    const auto& ui = controller_.ui();
    const auto& settings = ui.settings();
    const auto& physical = ui.physical();
    const auto* timing = controller_.selected_timing();
    out[0] = 1U; // Settings schema
    out[1] = 0x3fU; // Writable settings and the optional local RUN pause
    out[2] = static_cast<std::uint8_t>(ui.state());
    out[3] = static_cast<std::uint8_t>(8U | (ui.wants_run() ? 1U : 0U) |
                                      (ui.physical_ready() ? 2U : 0U) |
                                      (controller_.host_settings_writable() ? 4U : 0U) |
                                      (controller_.host_hold_available() ? 16U : 0U));
    out[4] = settings.timebase_index;
    out[5] = ScopeTimebaseCount;
    out[6] = static_cast<std::uint8_t>(settings.mode);
    out[7] = static_cast<std::uint8_t>(settings.edge);
    put16(out + 8U, settings.trigger_level_code);
    put16(out + 10U, settings.trigger_position_permille);
    put32(out + 12U, physical.microvolts_per_div);
    put32(out + 16U, timing == nullptr ? 0U : timing->timer_hz);
    put32(out + 20U, timing == nullptr ? 0U : timing->divider);
    out[24] = static_cast<std::uint8_t>(physical.coupling);
    out[25] = 1U; // Classic 10/20/50 us per decade catalogue
    put16(out + 26U, controller_.host_settings_revision());
    put32(out + 28U, scope_timebase_us(settings.timebase_index));
    respond(opcode, request_id, UsbStatus::Ok, 32U);
}

void UsbProtocolService::flush_tx() noexcept
{
    if (!tx_pending_ || port_.write == nullptr) { return; }
    if (port_.write(port_.context, tx_pending_, tx_size_)) { tx_pending_ = nullptr; }
}

bool UsbProtocolService::process_one(std::uint32_t now_ms) noexcept
{
    std::uint16_t magic_offset{};
    while (magic_offset + 1U < last_rx_size_ &&
           (rx_[magic_offset] != 0x44U || rx_[magic_offset + 1U] != 0x53U)) {
        ++magic_offset;
    }
    if (magic_offset != 0U) { shift_rx(rx_.data(), last_rx_size_, magic_offset); }
    if (last_rx_size_ >= 2U && (rx_[0] != 0x44U || rx_[1] != 0x53U)) {
        shift_rx(rx_.data(), last_rx_size_, static_cast<std::uint16_t>(last_rx_size_ - 1U));
    }
    if (last_rx_size_ < 12U) { return false; }
    const auto request_id = get16(rx_.data() + 4U);
    const auto opcode = rx_[3];
    const auto payload_size = get16(rx_.data() + 6U);
    if (rx_[2] != UsbProtocolVersion || request_id == 0U || payload_size > UsbProtocolMaximumPayload) {
        shift_rx(rx_.data(), last_rx_size_, 1U);
        if (request_id != 0U) { cache_response_ = false; respond(opcode, request_id, UsbStatus::Invalid); }
        return tx_pending_ != nullptr;
    }
    const auto frame_size = static_cast<std::uint16_t>(12U + payload_size);
    if (last_rx_size_ < frame_size) { return false; }
    const auto request_crc = usb_protocol_crc(rx_.data(), rx_.data() + 12U, payload_size);
    if (request_crc != get32(rx_.data() + 8U)) {
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
    dispatch(now_ms, opcode, request_id, rx_.data() + 12U, payload_size);
    shift_rx(rx_.data(), last_rx_size_, frame_size);
    return true;
}

void UsbProtocolService::receive(std::uint32_t now_ms) noexcept
{
    if (port_.read == nullptr || tx_pending_) { return; }
    const auto room = static_cast<std::uint16_t>(rx_.size() - last_rx_size_);
    if (room == 0U) { last_rx_size_ = 0U; return; }
    const auto cap = static_cast<std::uint16_t>(room < 16U ? room : 16U);
    const auto received = port_.read(port_.context, rx_.data() + last_rx_size_, cap);
    if (received > cap) { last_rx_size_ = 0U; return; }
    last_rx_size_ = static_cast<std::uint16_t>(last_rx_size_ + received);
    (void)process_one(now_ms);
}

void UsbProtocolService::dispatch(std::uint32_t, std::uint8_t opcode,
                                  std::uint16_t request_id,
                                  const std::uint8_t* payload,
                                  std::uint16_t payload_size) noexcept
{
    // Build response payload in place; respond() fills status/header/CRC.
    auto* out = response_buffer() + 13U;
    switch (static_cast<UsbOpcode>(opcode)) {
    case UsbOpcode::Info:
        if (payload_size != 0U) { respond(opcode, request_id, UsbStatus::Invalid); return; }
        out[0] = UsbProtocolVersion;
        put16(out + 1U, CaptureCapacity);
        out[3] = 12U; // raw ADC resolution
        out[4] = UsbProtocolMaximumSamplesPerRead;
        out[5] = 0U; out[6] = 0U; // reserved capability bits
        respond(opcode, request_id, UsbStatus::Ok, 7U);
        return;
    case UsbOpcode::Status: {
        if (payload_size != 4U) { respond(opcode, request_id, UsbStatus::Invalid); return; }
        const auto requested_job = get32(payload);
        if (requested_job != 0U && requested_job != host_job_) { respond(opcode, request_id, UsbStatus::Stale); return; }
        out[0] = static_cast<std::uint8_t>(controller_.ui().state());
        out[1] = static_cast<std::uint8_t>(controller_.fault());
        out[2] = static_cast<std::uint8_t>(controller_.work());
        out[3] = static_cast<std::uint8_t>(host_job_ == 0U ? HostCaptureState::None :
            controller_.host_capture_state(host_job_));
        put32(out + 4U, host_job_);
        out[8] = controller_.frame_available() ? 1U : 0U;
        put16(out + 9U, controller_.frame_metadata() == nullptr ? 0U : controller_.frame_metadata()->capture.length);
        out[11] = controller_.storage_busy() ? 1U : 0U;
        const auto status = capture_timed_out_ && requested_job == host_job_ ? UsbStatus::Timeout : UsbStatus::Ok;
        respond(opcode, request_id, status, 12U);
        return;
    }
    case UsbOpcode::BeginFrame:
    case UsbOpcode::BeginFrameExtended: {
        if (payload_size != 0U) { respond(opcode, request_id, UsbStatus::Invalid); return; }
        const auto token = controller_.begin_export();
        if (token == 0U) { respond(opcode, request_id, UsbStatus::Busy); return; }
        const auto& f = *controller_.frame_metadata();
        const auto calibration = controller_.frame_calibration();
        put32(out + 0U, token); // token occupies metadata payload bytes 1..4; status is frame prefix
        put16(out + 4U, f.capture.length);
        put16(out + 6U, f.capture.pretrigger);
        put32(out + 8U, f.timing.timer_hz);
        put32(out + 12U, f.timing.divider);
        put32(out + 16U, f.acquisition_epoch);
        put32(out + 20U, f.trigger_sequence);
        put32(out + 24U, f.physical.generation);
        put32(out + 28U, f.physical.microvolts_per_div);
        put32(out + 32U, static_cast<std::uint32_t>(calibration.zero_code_q16));
        put32(out + 36U, calibration.microvolts_per_code_q16);
        out[40] = calibration.gain_shift;
        out[41] = static_cast<std::uint8_t>(calibration.kind);
        out[42] = static_cast<std::uint8_t>(f.physical.coupling);
        out[43] = static_cast<std::uint8_t>(f.capture.mode);
        out[44] = static_cast<std::uint8_t>(f.capture.edge);
        // Bits 0..2 remain reserved for legacy filtering metadata.
        out[45] = f.edge_triggered ? 0x08U : 0U;
        out[46] = 0U;
        if (opcode == static_cast<std::uint8_t>(UsbOpcode::BeginFrameExtended)) {
            out[47] = controller_.frame_timebase_index();
            put16(out + 48U, f.capture.level);
            out[50] = 0U;
            respond(opcode, request_id, UsbStatus::Ok, 51U);
        } else {
            respond(opcode, request_id, UsbStatus::Ok, 47U);
        }
        return;
    }
    case UsbOpcode::ReadSamples: {
        if (payload_size != 7U) { respond(opcode, request_id, UsbStatus::Invalid); return; }
        const auto token = get32(payload);
        const auto offset = get16(payload + 4U);
        const auto count = payload[6];
        const auto* samples = controller_.export_samples(token);
        if (samples == nullptr) {
            respond(opcode, request_id, UsbStatus::Stale); return;
        }
        const auto length = controller_.frame_metadata()->capture.length;
        if (count == 0U || count > UsbProtocolMaximumSamplesPerRead ||
            offset > length || count > static_cast<std::uint16_t>(length - offset)) {
            respond(opcode, request_id, UsbStatus::Invalid); return;
        }
        put32(out, token); put16(out + 4U, offset); out[6] = count;
        put_samples(out + 7U, samples + offset, count);
        respond(opcode, request_id, UsbStatus::Ok,
                static_cast<std::uint8_t>(7U + static_cast<std::uint16_t>(count) * 2U));
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
        if (payload_size != 4U) { respond(opcode, request_id, UsbStatus::Invalid); return; }
        const auto timeout = get32(payload);
        if (timeout == 0U || timeout > 600000U) { respond(opcode, request_id, UsbStatus::Invalid); return; }
        if (host_job_ != 0U && controller_.host_capture_state(host_job_) == HostCaptureState::Pending) {
            respond(opcode, request_id, UsbStatus::Busy); return;
        }
        const auto job = controller_.request_host_single(opcode == static_cast<std::uint8_t>(UsbOpcode::CaptureSelected));
        if (job == 0U) { respond(opcode, request_id, UsbStatus::Busy); return; }
        host_job_ = job;
        capture_deadline_ms_ = last_activity_ms_ + timeout;
        capture_timed_out_ = false;
        put32(out, job);
        respond(opcode, request_id, UsbStatus::Ok, 4U);
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
        if (payload_size != (hold ? 2U : trigger ? 7U : 4U)) {
            respond(opcode, request_id, UsbStatus::Invalid); return;
        }
        HostSettingsResult result;
        const auto revision = get16(payload);
        if (hold) {
            result = controller_.hold_from_host(revision);
        } else {
            auto settings = controller_.ui().settings();
            if (trigger) {
                settings.edge = static_cast<TriggerEdge>(payload[2]);
                settings.trigger_level_code = get16(payload + 3U);
                settings.trigger_position_permille = get16(payload + 5U);
            } else {
                settings.timebase_index = payload[2];
                settings.mode = static_cast<TriggerMode>(payload[3]);
            }
            result = controller_.apply_host_settings(revision, settings);
        }
        // This enum maps directly to the four corresponding wire statuses.
        static_assert(static_cast<unsigned>(HostSettingsResult::Applied) == static_cast<unsigned>(UsbStatus::Ok));
        static_assert(static_cast<unsigned>(HostSettingsResult::Invalid) == static_cast<unsigned>(UsbStatus::Invalid));
        static_assert(static_cast<unsigned>(HostSettingsResult::Busy) == static_cast<unsigned>(UsbStatus::Busy));
        static_assert(static_cast<unsigned>(HostSettingsResult::Stale) == static_cast<unsigned>(UsbStatus::Stale));
        if (result == HostSettingsResult::Applied) { respond_settings(opcode, request_id); }
        else { respond(opcode, request_id, static_cast<UsbStatus>(result)); }
        return;
    }
    default:
        respond(opcode, request_id, UsbStatus::Unsupported);
        return;
    }
}

void UsbProtocolService::step(std::uint32_t now_ms) noexcept
{
    if (port_.session == nullptr) { return; }
    const auto current = port_.session(port_.context);
    if (current != session_) {
        disconnect();
        session_ = current;
        last_activity_ms_ = now_ms;
        if (session_ == 0U) { return; }
    }
    if (session_ == 0U) { return; }
    service_timeouts(now_ms);
    service_capture(now_ms);
    flush_tx();
    if (tx_pending_) { return; }
    receive(now_ms);
}

} // namespace dso
