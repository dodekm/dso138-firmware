#ifndef DSO_STM32_USB_HPP
#define DSO_STM32_USB_HPP

#include "dso_usb_cdc.hpp"
#include "dso_usb_transport.hpp"

#ifndef DSO_USB_VID
#define DSO_USB_VID 0U
#endif
#ifndef DSO_USB_PID
#define DSO_USB_PID 0U
#endif
#ifndef DSO_USB_STOCK_BENCH_TRIAL
#define DSO_USB_STOCK_BENCH_TRIAL 0
#endif
static_assert(DSO_USB_STOCK_BENCH_TRIAL == 0 || DSO_USB_STOCK_BENCH_TRIAL == 1,
              "Invalid stock USB bench-trial profile");

namespace dso {

namespace detail {
// Pure RM0008 EPnR write builder, exposed for exhaustive toggle/W0C tests.
[[nodiscard]] std::uint16_t usb_ep_compose_write(std::uint16_t current,
    std::uint16_t stable_fields, std::uint8_t rx_status, std::uint8_t tx_status,
    bool clear_ctr_rx, bool clear_ctr_tx,
    bool reset_dto_rx, bool reset_dto_tx) noexcept;
}

// Every field is false by default. A caller must provide documented physical
// inspection/electrical qualification before start() is allowed to touch HW.
struct Stm32UsbQualification final {
    bool pa11_dm_pa12_dp_confirmed{};
    bool external_3v3_dplus_pullup_switch_confirmed{};
    bool pa9_vbus_divider_confirmed{};
    bool pullup_switch_hardware_default_off_confirmed{};
    bool self_power_and_ground_safety_confirmed{};
    [[nodiscard]] bool complete() const noexcept
    {
        return pa11_dm_pa12_dp_confirmed && external_3v3_dplus_pullup_switch_confirmed &&
            pa9_vbus_divider_confirmed && pullup_switch_hardware_default_off_confirmed &&
            self_power_and_ground_safety_confirmed;
    }
};

// STM32F103 USB device FS adapter. Does not own USB_HP/LP vector definitions;
// the application ISR calls irq_notify(), while foreground calls poll().
class Stm32UsbDevice final {
public:
    Stm32UsbDevice() noexcept;
    Stm32UsbDevice(const Stm32UsbDevice&) = delete;
    Stm32UsbDevice& operator=(const Stm32UsbDevice&) = delete;
    [[nodiscard]] bool start(const Stm32UsbQualification& qualification,
                             std::uint16_t vendor_id = static_cast<std::uint16_t>(DSO_USB_VID),
                             std::uint16_t product_id = static_cast<std::uint16_t>(DSO_USB_PID)) noexcept;
    void stop() noexcept;
    // ISR-safe: mask only this NVIC IRQ and publish one pending bit.
    void irq_notify() noexcept;
    // Foreground-only, bounded service of at most two USB event causes.
    void poll() noexcept;
    [[nodiscard]] UsbStreamPort stream_port() noexcept { return cdc_.stream_port(); }
    [[nodiscard]] bool started() const noexcept { return started_; }
    [[nodiscard]] bool faulted() const noexcept { return faulted_; }
    [[nodiscard]] bool vbus_present() const noexcept;

private:
    static bool send_in(void*, std::uint8_t, const std::uint8_t*, std::uint8_t) noexcept;
    static void set_rx_enabled(void*, std::uint8_t, bool) noexcept;
    static void set_halt(void*, std::uint8_t, bool) noexcept;
    static void set_address(void*, std::uint8_t) noexcept;
    static void set_configuration(void*, bool) noexcept;
    static void abort_control(void*) noexcept;
    static void cancel_bulk(void*) noexcept;
    [[nodiscard]] bool submit_in(std::uint8_t, const std::uint8_t*, std::uint8_t) noexcept;
    void configure_endpoints(bool) noexcept;
    [[nodiscard]] bool service_event() noexcept;
    void service_ctr() noexcept;
    void service_reset() noexcept;
    void fail_closed() noexcept;
    void set_dplus_pullup(bool) noexcept;
    void set_ep_status(std::uint8_t endpoint, bool tx, std::uint8_t status) noexcept;
    void reset_ep_toggle(std::uint8_t endpoint, bool transmit) noexcept;
    void quiesce_endpoint(std::uint8_t endpoint, bool transmit,
                          std::uint8_t final_status, bool reset_toggle) noexcept;
    void finish_quiesce() noexcept;

    UsbCdcDevice cdc_;
    std::uint32_t startup_cycle_{};
    std::uint32_t quiesce_cycle_{};
    volatile bool irq_pending_{};
    bool started_{};
    bool faulted_{};
    bool peripheral_owned_{};
    bool pins_owned_{};
    bool pullup_on_{};
    bool startup_wait_{};
    bool configured_{};
    bool usbpre_saved_{};
    bool vbus_seen_{};
    bool suspended_{};
    bool tx_busy_[3U]{};
    std::uint8_t quiesce_mask_{};
    std::uint8_t quiesce_target_[4U]{};
    std::uint8_t quiesce_reset_toggle_mask_{};
};

} // namespace dso
#endif
