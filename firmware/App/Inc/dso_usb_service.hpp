#ifndef DSO_USB_SERVICE_HPP
#define DSO_USB_SERVICE_HPP

#include "dso_usb_protocol.hpp"
#include "dso_ui_controller.hpp"
#include <array>

namespace dso {

// Compact foreground protocol adapter. step() reads <=16 transport bytes,
// handles no more than one command, and uses fixed buffers only. It is not an
// ISR API. The controller and transport must outlive this object.
class UsbProtocolService final {
public:
    UsbProtocolService(ScopeController& controller, UsbStreamPort port) noexcept;
    UsbProtocolService(const UsbProtocolService&) = delete;
    UsbProtocolService& operator=(const UsbProtocolService&) = delete;

    void step(std::uint32_t now_ms) noexcept;
    [[nodiscard]] bool active() const noexcept { return session_ != 0U; }

private:
    void disconnect() noexcept;
    void service_timeouts(std::uint32_t now_ms) noexcept;
    void service_capture(std::uint32_t now_ms) noexcept;
    void receive(std::uint32_t now_ms) noexcept;
    [[nodiscard]] bool process_one(std::uint32_t now_ms) noexcept;
    void dispatch(std::uint32_t now_ms, std::uint8_t opcode,
                  std::uint16_t request_id,
                  const std::uint8_t* payload, std::uint16_t payload_size) noexcept;
    void respond(std::uint8_t opcode, std::uint16_t request_id, UsbStatus status,
                 std::uint8_t payload_size = 0U) noexcept;
    void respond_settings(std::uint8_t opcode, std::uint16_t request_id) noexcept;
    void flush_tx() noexcept;
    void cancel_owned() noexcept;
    [[nodiscard]] std::uint8_t* response_buffer() noexcept
    { return cache_response_ ? replay_.data() : tx_.data(); }

    ScopeController& controller_;
    UsbStreamPort port_;
    std::array<std::uint8_t, UsbProtocolMaximumFrame> rx_{};
    std::array<std::uint8_t, UsbProtocolMaximumFrame> tx_{};
    std::array<std::uint8_t, UsbProtocolMaximumFrame> replay_{};
    const std::uint8_t* tx_pending_{};
    std::uint32_t session_{};
    std::uint32_t last_activity_ms_{};
    std::uint32_t capture_deadline_ms_{};
    std::uint32_t host_job_{};
    std::uint32_t last_request_crc_{};
    std::uint16_t last_request_id_{};
    std::uint16_t last_rx_size_{};
    std::uint8_t tx_size_{};
    std::uint8_t replay_size_{};
    std::uint8_t last_opcode_{};
    bool cache_response_{};
    bool capture_timed_out_{};
};

} // namespace dso
#endif
