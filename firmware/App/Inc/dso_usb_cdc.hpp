#ifndef DSO_USB_CDC_HPP
#define DSO_USB_CDC_HPP

#include "dso_usb_protocol.hpp"
#include <array>
#include <cstdint>

namespace dso {

// Small USB CDC-ACM class core. The register/PMA backend owns packet timing,
// endpoint toggle semantics and IRQ routing; callbacks only queue bounded data.
struct UsbCdcPort final {
    void* context{};
    bool (*send_in)(void*, std::uint8_t endpoint, const std::uint8_t*, std::uint8_t) noexcept{};
    void (*set_rx_enabled)(void*, std::uint8_t endpoint, bool) noexcept{};
    void (*set_halt)(void*, std::uint8_t endpoint_address, bool) noexcept{};
    void (*set_address)(void*, std::uint8_t) noexcept{};
    void (*set_configuration)(void*, bool) noexcept{};
    void (*abort_control)(void*) noexcept{};
    void (*cancel_bulk)(void*) noexcept{};
};

class UsbCdcDevice final {
public:
    explicit UsbCdcDevice(UsbCdcPort port) noexcept : port_{port} {}
    UsbCdcDevice(const UsbCdcDevice&) = delete;
    UsbCdcDevice& operator=(const UsbCdcDevice&) = delete;

    [[nodiscard]] bool initialize(std::uint16_t vendor_id, std::uint16_t product_id) noexcept;
    void on_vbus(bool present) noexcept;
    void on_reset() noexcept;
    void on_suspend() noexcept;
    void on_resume() noexcept;
    void on_setup(const std::uint8_t setup[8]) noexcept;
    void on_out(std::uint8_t endpoint, const std::uint8_t*, std::uint8_t) noexcept;
    void on_in_complete(std::uint8_t endpoint) noexcept;
    void poll() noexcept;

    [[nodiscard]] std::uint32_t session() const noexcept;
    [[nodiscard]] UsbStreamPort stream_port() noexcept;
    [[nodiscard]] bool configured() const noexcept { return configured_; }
    [[nodiscard]] const std::uint8_t* device_descriptor() const noexcept { return device_.data(); }
    [[nodiscard]] const std::uint8_t* configuration_descriptor() const noexcept;

private:
    enum class ControlPhase : std::uint8_t { Idle, DataIn, DataOutLineCoding, StatusIn, StatusOut };
    enum class Deferred : std::uint8_t { None, Address, Configuration, LineState, LineCoding, Halt };
    void bump_session() noexcept;
    void stall_control() noexcept;
    void control_in(const std::uint8_t*, std::uint16_t, std::uint16_t) noexcept;
    void next_control_packet() noexcept;
    void control_status_in(Deferred, std::uint8_t, std::uint16_t = 0U) noexcept;
    void commit_deferred() noexcept;
    void configure(bool) noexcept;
    void set_rx(std::uint8_t endpoint, bool enabled) noexcept;
    [[nodiscard]] bool known_endpoint(std::uint8_t address) const noexcept;
    [[nodiscard]] bool endpoint_halted(std::uint8_t address) const noexcept;

    static std::uint32_t stream_session(void*) noexcept;
    static std::uint16_t stream_read(void*, std::uint8_t*, std::uint16_t) noexcept;
    static bool stream_write(void*, const std::uint8_t*, std::uint16_t) noexcept;

    UsbCdcPort port_;
    std::array<std::uint8_t, 18U> device_{};
    std::array<std::uint8_t, 7U> line_coding_{{0x00U,0xc2U,0x01U,0x00U,0x00U,0x00U,0x08U}};
    // These payloads are consumed only after their valid length/queued flags
    // are published; omit redundant zero-fill while keeping those gates zeroed.
    std::array<std::uint8_t, 7U> pending_line_coding_;
    std::array<std::uint8_t, 64U> bulk_tx_;
    std::array<std::uint8_t, 64U> bulk_rx_;
    const std::uint8_t* control_data_{};
    std::uint32_t session_epoch_{};
    std::uint16_t vendor_id_{};
    std::uint16_t product_id_{};
    std::uint16_t control_length_{};
    std::uint16_t control_requested_{};
    std::uint16_t control_offset_{};
    std::uint16_t control_packet_size_{};
    std::uint16_t line_state_{};
    std::uint16_t pending_line_state_{};
    std::uint8_t address_{};
    std::uint8_t configuration_value_{};
    std::uint8_t interface_alt_[2U]{};
    std::uint8_t pending_value_{};
    std::uint8_t pending_ep_{};
    std::uint8_t bulk_tx_size_{};
    std::uint8_t bulk_rx_size_{};
    std::uint8_t bulk_rx_offset_{};
    ControlPhase control_phase_{ControlPhase::Idle};
    Deferred deferred_{Deferred::None};
    bool initialized_{};
    bool vbus_present_{};
    bool suspended_{};
    bool configured_{};
    bool bulk_tx_queued_{};
    bool bulk_tx_inflight_{};
    bool bulk_zlp_pending_{};
    bool bulk_zlp_inflight_{};
    bool bulk_rx_pending_{};
    bool control_zlp_after_data_{};
    bool control_zlp_inflight_{};
    bool halt_ep1_in_{};
    bool halt_ep1_out_{};
    bool halt_ep2_in_{};
};

} // namespace dso
#endif
