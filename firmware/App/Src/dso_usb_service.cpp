#include "dso_usb_service.hpp"
#include "dso_ui_controller.hpp"


namespace dso {
namespace {
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

void UsbProtocolService::flush_tx() noexcept
{
    if (!tx_pending_ || port_.write == nullptr) { return; }
    if (port_.write(port_.context, tx_pending_, tx_size_)) { tx_pending_ = nullptr; }
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
