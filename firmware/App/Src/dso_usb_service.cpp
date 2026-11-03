#include "dso_usb_service.hpp"

namespace dso {
namespace {
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
void shift_rx(std::uint8_t* bytes, std::uint16_t& size, std::uint16_t count) noexcept
{
    if (count >= size) { size = 0U; return; }
    for (std::uint16_t i{}; i < static_cast<std::uint16_t>(size - count); ++i) { bytes[i] = bytes[i + count]; }
    size = static_cast<std::uint16_t>(size - count);
}
bool deadline_reached(std::uint32_t now, std::uint32_t deadline) noexcept
{ return static_cast<std::int32_t>(now - deadline) >= 0; }
}

UsbProtocolService::UsbProtocolService(ScopeController& controller, UsbStreamPort port) noexcept
    : controller_{controller}, port_{port}
{}

void UsbProtocolService::cancel_owned() noexcept
{
    if (export_token_ != 0U) { controller_.end_export(export_token_); }
    export_token_ = 0U;
    export_view_ = {};
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
    last_rx_size_ = 0U;
    tx_size_ = replay_size_ = 0U;
    tx_pending_ = false;
    last_request_id_ = 0U;
    last_request_crc_ = 0U;
    last_opcode_ = 0U;
    cache_response_ = false;
}

bool UsbProtocolService::valid_controller_state() const noexcept
{
    if (export_token_ == 0U) { return true; }
    std::uint16_t sample{};
    return controller_.export_sample(export_token_, 0U, sample);
}

void UsbProtocolService::service_timeouts(std::uint32_t now_ms) noexcept
{
    if (session_ != 0U && static_cast<std::uint32_t>(now_ms - last_activity_ms_) >= 5000U) {
        cancel_owned();
        last_request_id_ = 0U;
        replay_size_ = 0U;
    }
    if (!valid_controller_state()) {
        if (export_token_ != 0U) { controller_.end_export(export_token_); }
        export_token_ = 0U;
        export_view_ = {};
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

void UsbProtocolService::queue_frame(const std::uint8_t* frame, std::uint8_t size) noexcept
{
    if (frame == nullptr || size == 0U || size > UsbProtocolMaximumFrame) { return; }
    for (std::uint8_t i{}; i < size; ++i) { tx_[i] = frame[i]; }
    tx_size_ = size;
    tx_pending_ = true;
}

void UsbProtocolService::respond(std::uint8_t opcode, std::uint16_t request_id,
                                 UsbStatus status, std::uint8_t payload_size) noexcept
{
    if (payload_size > 51U) { return; }
    auto* frame = tx_.data();
    frame[0] = 0x44U; frame[1] = 0x53U; frame[2] = UsbProtocolVersion;
    frame[3] = static_cast<std::uint8_t>(opcode | 0x80U);
    put16(frame + 4U, request_id);
    const auto total_payload = static_cast<std::uint8_t>(payload_size + 1U);
    put16(frame + 6U, total_payload);
    frame[12] = static_cast<std::uint8_t>(status);
    put32(frame + 8U, usb_protocol_crc(frame, frame + 12U, total_payload));
    tx_size_ = static_cast<std::uint8_t>(12U + total_payload);
    tx_pending_ = true;
    if (cache_response_) {
        for (std::uint8_t i{}; i < tx_size_; ++i) { replay_[i] = tx_[i]; }
        replay_size_ = tx_size_;
    }
}

void UsbProtocolService::flush_tx() noexcept
{
    if (!tx_pending_ || port_.write == nullptr) { return; }
    if (port_.write(port_.context, tx_.data(), tx_size_)) {
        tx_pending_ = false;
        tx_size_ = 0U;
    }
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
        return tx_pending_;
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
            queue_frame(replay_.data(), replay_size_);
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
    auto* out = tx_.data() + 13U;
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
    case UsbOpcode::BeginFrame: {
        if (payload_size != 0U) { respond(opcode, request_id, UsbStatus::Invalid); return; }
        if (!controller_.begin_export(export_view_)) { respond(opcode, request_id, UsbStatus::Busy); return; }
        export_token_ = export_view_.token;
        const auto& f = export_view_.frame;
        out[0] = static_cast<std::uint8_t>(export_token_);
        put32(out + 0U, export_token_); // token occupies metadata payload bytes 1..4; status is frame prefix
        put16(out + 4U, f.capture.length);
        put16(out + 6U, f.capture.pretrigger);
        put32(out + 8U, f.timing.timer_hz);
        put32(out + 12U, f.timing.divider);
        put32(out + 16U, f.acquisition_epoch);
        put32(out + 20U, f.trigger_sequence);
        put32(out + 24U, f.physical.generation);
        put32(out + 28U, f.physical.microvolts_per_div);
        put32(out + 32U, static_cast<std::uint32_t>(export_view_.calibration.zero_code_q16));
        put32(out + 36U, export_view_.calibration.microvolts_per_code_q16);
        out[40] = export_view_.calibration.gain_shift;
        out[41] = static_cast<std::uint8_t>(export_view_.calibration.kind);
        out[42] = static_cast<std::uint8_t>(f.physical.coupling);
        out[43] = static_cast<std::uint8_t>(f.capture.mode);
        out[44] = static_cast<std::uint8_t>(f.capture.edge);
        // Bits 0..2 remain reserved for legacy filtering metadata.
        out[45] = f.edge_triggered ? 0x08U : 0U;
        out[46] = out[47] = 0U;
        // respond prepends status at payload[0], so wire fields begin one byte later.
        respond(opcode, request_id, UsbStatus::Ok, 47U);
        return;
    }
    case UsbOpcode::ReadSamples: {
        if (payload_size != 7U) { respond(opcode, request_id, UsbStatus::Invalid); return; }
        const auto token = get32(payload);
        const auto offset = get16(payload + 4U);
        const auto count = payload[6];
        if (token == 0U || token != export_token_) { respond(opcode, request_id, UsbStatus::Stale); return; }
        if (count == 0U || count > UsbProtocolMaximumSamplesPerRead ||
            offset > export_view_.frame.capture.length ||
            count > static_cast<std::uint16_t>(export_view_.frame.capture.length - offset)) {
            respond(opcode, request_id, UsbStatus::Invalid); return;
        }
        put32(out, token); put16(out + 4U, offset); out[6] = count;
        for (std::uint8_t i{}; i < count; ++i) {
            std::uint16_t sample{};
            if (!controller_.export_sample(token, static_cast<std::uint16_t>(offset + i), sample)) {
                export_token_ = 0U; export_view_ = {};
                respond(opcode, request_id, UsbStatus::Stale); return;
            }
            put16(out + 7U + static_cast<std::uint16_t>(i) * 2U, sample);
        }
        respond(opcode, request_id, UsbStatus::Ok,
                static_cast<std::uint8_t>(7U + static_cast<std::uint16_t>(count) * 2U));
        return;
    }
    case UsbOpcode::EndFrame: {
        if (payload_size != 4U) { respond(opcode, request_id, UsbStatus::Invalid); return; }
        const auto token = get32(payload);
        if (token == 0U || token != export_token_) { respond(opcode, request_id, UsbStatus::Stale); return; }
        controller_.end_export(token);
        export_token_ = 0U; export_view_ = {};
        respond(opcode, request_id, UsbStatus::Ok);
        return;
    }
    case UsbOpcode::CaptureSingle: {
        if (payload_size != 4U) { respond(opcode, request_id, UsbStatus::Invalid); return; }
        const auto timeout = get32(payload);
        if (timeout == 0U || timeout > 600000U) { respond(opcode, request_id, UsbStatus::Invalid); return; }
        if (host_job_ != 0U && controller_.host_capture_state(host_job_) == HostCaptureState::Pending) {
            respond(opcode, request_id, UsbStatus::Busy); return;
        }
        const auto job = controller_.request_host_single();
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
