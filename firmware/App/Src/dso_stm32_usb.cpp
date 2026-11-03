#include "dso_stm32_usb.hpp"
#include "main.h"
#include <cstdint>

namespace dso {
namespace {
constexpr std::uintptr_t PmaBase{static_cast<std::uintptr_t>(0x40006000UL)};
constexpr std::uint16_t BtableBase{0U};
constexpr std::uint16_t Ep0RxPma{0x40U};
constexpr std::uint16_t Ep0TxPma{0x80U};
constexpr std::uint16_t Ep1RxPma{0xc0U};
constexpr std::uint16_t Ep1TxPma{0x100U};
constexpr std::uint16_t Ep2TxPma{0x140U};
constexpr std::uint16_t BtableRx64{0x8400U};
constexpr std::uint8_t EpStatusDisabled{0U};
constexpr std::uint8_t EpStatusStall{1U};
constexpr std::uint8_t EpStatusNak{2U};
constexpr std::uint8_t EpStatusValid{3U};
constexpr std::uint16_t EpStableMask{USB_EP_TYPE_MASK | USB_EP_KIND | USB_EPADDR_FIELD};
constexpr std::uint16_t UsbEvents{USB_ISTR_RESET | USB_ISTR_SUSP | USB_ISTR_WKUP |
    USB_ISTR_CTR | USB_ISTR_ERR | USB_ISTR_PMAOVR | USB_ISTR_ESOF};

constexpr std::uint8_t quiesce_index(std::uint8_t endpoint, bool tx) noexcept
{ return static_cast<std::uint8_t>((endpoint - 1U) + (tx ? 2U : 0U)); }

constexpr std::uint8_t quiesce_bit(std::uint8_t endpoint, bool tx) noexcept
{ return static_cast<std::uint8_t>(1U << quiesce_index(endpoint, tx)); }

volatile std::uint16_t* ep_register(std::uint8_t ep) noexcept
{
    return reinterpret_cast<volatile std::uint16_t*>(USB_BASE + (std::uint32_t{ep} * 4U));
}
volatile std::uint16_t* pma_word(std::uint16_t logical) noexcept
{
    const auto address = PmaBase + (static_cast<std::uintptr_t>(logical) * 2U);
    return reinterpret_cast<volatile std::uint16_t*>(address);
}
void pma_write16(std::uint16_t logical, std::uint16_t value) noexcept
{ *pma_word(logical) = value; }
std::uint16_t pma_read16(std::uint16_t logical) noexcept
{ return *pma_word(logical); }
void pma_write(std::uint16_t logical, const std::uint8_t* data, std::uint8_t size) noexcept
{
    for (std::uint16_t i{}; i < size; i = static_cast<std::uint16_t>(i + 2U)) {
        const auto lo = data == nullptr ? 0U : data[i];
        const auto hi = static_cast<std::uint16_t>(i + 1U < size && data != nullptr ? data[i + 1U] : 0U);
        pma_write16(static_cast<std::uint16_t>(logical + i), static_cast<std::uint16_t>(lo | (hi << 8U)));
    }
}
void pma_read(std::uint16_t logical, std::uint8_t* data, std::uint8_t size) noexcept
{
    for (std::uint16_t i{}; i < size; i = static_cast<std::uint16_t>(i + 2U)) {
        const auto word = pma_read16(static_cast<std::uint16_t>(logical + i));
        data[i] = static_cast<std::uint8_t>(word);
        if (i + 1U < size) { data[i + 1U] = static_cast<std::uint8_t>(word >> 8U); }
    }
}
void btable_write(std::uint8_t ep, std::uint8_t field, std::uint16_t value) noexcept
{
    const auto logical = static_cast<std::uint16_t>(BtableBase + (std::uint16_t{ep} * 8U) + (std::uint16_t{field} * 2U));
    pma_write16(logical, value);
}
std::uint16_t btable_read(std::uint8_t ep, std::uint8_t field) noexcept
{
    const auto logical = static_cast<std::uint16_t>(BtableBase + (std::uint16_t{ep} * 8U) + (std::uint16_t{field} * 2U));
    return pma_read16(logical);
}
void clear_istr(std::uint16_t flags) noexcept
{ USB->ISTR = static_cast<std::uint16_t>(~flags); }
bool reset_pending() noexcept
{
    // Captured F103 reset sequence cleared EP0R before RESET appeared in ISTR.
    // service_reset() rebuilds EP0 after the reset event is serviced.
    const auto ep0 = *ep_register(0U);
    return (USB->ISTR & USB_ISTR_RESET) != 0U ||
        (ep0 & USB_EP_TYPE_MASK) == 0U;
}
}

std::uint16_t detail::usb_ep_compose_write(std::uint16_t current,
    std::uint16_t stable_fields, std::uint8_t rx_status, std::uint8_t tx_status,
    bool clear_ctr_rx, bool clear_ctr_tx,
    bool reset_dto_rx, bool reset_dto_tx) noexcept
{
    if (rx_status > 3U || tx_status > 3U) { return 0U; }
    constexpr std::uint16_t CtrRx{0x8000U};
    constexpr std::uint16_t CtrTx{0x0080U};
    constexpr std::uint16_t DtoRx{0x4000U};
    constexpr std::uint16_t DtoTx{0x0040U};
    constexpr std::uint16_t RxStat{0x3000U};
    constexpr std::uint16_t TxStat{0x0030U};
    auto value = static_cast<std::uint16_t>(stable_fields & EpStableMask);
    if (!clear_ctr_rx) { value = static_cast<std::uint16_t>(value | CtrRx); }
    if (!clear_ctr_tx) { value = static_cast<std::uint16_t>(value | CtrTx); }
    // SETUP is read-only; clearing CTR_RX clears its hardware indication.
    const auto old_rx = static_cast<std::uint16_t>((current & RxStat) >> 12U);
    const auto old_tx = static_cast<std::uint16_t>((current & TxStat) >> 4U);
    value = static_cast<std::uint16_t>(value | ((old_rx ^ rx_status) << 12U) |
        ((old_tx ^ tx_status) << 4U));
    if (reset_dto_rx && (current & DtoRx) != 0U) { value = static_cast<std::uint16_t>(value | DtoRx); }
    if (reset_dto_tx && (current & DtoTx) != 0U) { value = static_cast<std::uint16_t>(value | DtoTx); }
    return value;
}

Stm32UsbDevice::Stm32UsbDevice() noexcept
    : cdc_{UsbCdcPort{this, &send_in, &set_rx_enabled, &set_halt,
                     &set_address, &set_configuration, &abort_control, &cancel_bulk}}
{}

bool Stm32UsbDevice::start(const Stm32UsbQualification& qualification,
                           std::uint16_t vendor_id, std::uint16_t product_id) noexcept
{
#if DSO_USB_STOCK_BENCH_TRIAL
    static_cast<void>(qualification); // Explicit bench profile; no fabricated board qualification.
#endif
    if (started_ ||
        (!DSO_USB_STOCK_BENCH_TRIAL && !qualification.complete()) ||
        vendor_id == 0U || product_id == 0U ||
        vendor_id == 0xffffU || product_id == 0xffffU || SystemCoreClock != 72000000U ||
        (CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0U ||
        (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U ||
        (RCC->APB1ENR & (RCC_APB1ENR_USBEN | RCC_APB1ENR_CAN1EN)) != 0U ||
        NVIC_GetEnableIRQ(USB_LP_CAN1_RX0_IRQn) != 0U ||
        (RCC->APB2ENR & RCC_APB2ENR_IOPAEN) == 0U ||
        (RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL ||
        (RCC->CR & RCC_CR_PLLRDY) == 0U || HAL_RCC_GetSysClockFreq() != 72000000U ||
        (HAL_RCC_GetPCLK1Freq() != 36000000U || HAL_RCC_GetPCLK2Freq() != 72000000U) ||
        (RCC->CFGR & RCC_CFGR_PPRE1) != RCC_CFGR_PPRE1_DIV2 ||
        (RCC->CFGR & RCC_CFGR_ADCPRE) != RCC_CFGR_ADCPRE_DIV6
 #if !DSO_USB_STOCK_BENCH_TRIAL
        || (GPIOA->LCKR & (GPIO_PIN_4 | GPIO_PIN_9)) != 0U ||
        (GPIOA->CRL & (0x0fUL << 16U)) != (0x04UL << 16U) ||
        (GPIOA->CRH & (0x0fUL << 4U)) != (0x04UL << 4U)
 #endif
        ) { return false; }
    if (!cdc_.initialize(vendor_id, product_id)) { return false; }
#if DSO_USB_STOCK_BENCH_TRIAL
    cdc_.on_vbus(true); // Explicit bench assumption only; no board VBUS sensor is read.
    // A fixed 20 ms PA12 GPIO-low interval makes an already-connected bench
    // host observe detach before the USB clock reconnects PA12 to the PHY.
    GPIOA->BSRR = static_cast<std::uint32_t>(GPIO_PIN_12) << 16U;
    GPIOA->CRH = (GPIOA->CRH & ~(0x0fUL << 16U)) | (0x06UL << 16U);
    HAL_Delay(20U);
    GPIOA->CRH = (GPIOA->CRH & ~(0x0fUL << 16U)) | (0x04UL << 16U);
#endif

    usbpre_saved_ = (RCC->CFGR & RCC_CFGR_USBPRE) != 0U;
    HAL_NVIC_DisableIRQ(USB_LP_CAN1_RX0_IRQn);
    HAL_NVIC_ClearPendingIRQ(USB_LP_CAN1_RX0_IRQn);
#if DSO_USB_STOCK_BENCH_TRIAL
    pins_owned_ = false;
#else
    // Default-off first; only after the latch is low may PA4 become an output.
    GPIOA->BSRR = static_cast<std::uint32_t>(GPIO_PIN_4) << 16U;
    GPIOA->CRL = (GPIOA->CRL & ~(0x0fUL << 16U)) | (0x02UL << 16U);
    GPIOA->CRH = (GPIOA->CRH & ~(0x0fUL << 4U)) | (0x04UL << 4U);
    pins_owned_ = true;
#endif
    RCC->CFGR &= ~RCC_CFGR_USBPRE; // 72 MHz PLL / 1.5 = 48 MHz; preserve every other clock field.
    RCC->APB1ENR |= RCC_APB1ENR_USBEN;
    peripheral_owned_ = true;
    USB->CNTR = static_cast<std::uint16_t>(USB_CNTR_FRES | USB_CNTR_PDWN);
    USB->CNTR = static_cast<std::uint16_t>(USB_CNTR_FRES); // Power up PHY with reset held; release asynchronously in poll().
    USB->DADDR = USB_DADDR_EF;
    USB->BTABLE = BtableBase;
    USB->ISTR = static_cast<std::uint16_t>(0U);
    startup_cycle_ = DWT->CYCCNT;
    startup_wait_ = true;
    started_ = true;
    faulted_ = false;
    vbus_seen_ = false;
    irq_pending_ = false;
    tx_busy_[0] = tx_busy_[1] = tx_busy_[2] = false;
    quiesce_mask_ = quiesce_reset_toggle_mask_ = 0U;
    suspended_ = false;
    HAL_NVIC_SetPriority(USB_LP_CAN1_RX0_IRQn, 2U, 0U); // DMA1 Channel 1 acquisition priority is 1.
    peripheral_owned_ = true;
    return true;
}

void Stm32UsbDevice::set_dplus_pullup(bool enabled) noexcept
{
#if DSO_USB_STOCK_BENCH_TRIAL
    static_cast<void>(enabled); // The stock resistor is fixed; never touch PA4 or PA9.
    pullup_on_ = started_ && !startup_wait_ && !faulted_;
#else
    const bool on = enabled && started_ && !startup_wait_ && !faulted_ && vbus_present();
    GPIOA->BSRR = on ? GPIO_PIN_4 : (static_cast<std::uint32_t>(GPIO_PIN_4) << 16U);
    pullup_on_ = on;
#endif
}

bool Stm32UsbDevice::vbus_present() const noexcept
{
#if DSO_USB_STOCK_BENCH_TRIAL
    return false; // No board VBUS input is sampled in this profile.
#else
    return started_ && (GPIOA->IDR & GPIO_PIN_9) != 0U;
#endif
}

void Stm32UsbDevice::stop() noexcept
{
    if (!started_ && !peripheral_owned_) { return; }
#if DSO_USB_STOCK_BENCH_TRIAL
    HAL_NVIC_DisableIRQ(USB_LP_CAN1_RX0_IRQn);
    HAL_NVIC_ClearPendingIRQ(USB_LP_CAN1_RX0_IRQn);
    irq_pending_ = false;
    if (peripheral_owned_) {
        cdc_.on_reset();
        USB->CNTR = static_cast<std::uint16_t>(USB_CNTR_FRES | USB_CNTR_PDWN);
        RCC->APB1ENR &= ~RCC_APB1ENR_USBEN;
        peripheral_owned_ = false;
    }
    GPIOA->BSRR = static_cast<std::uint32_t>(GPIO_PIN_12) << 16U;
    GPIOA->CRH = (GPIOA->CRH & ~(0x0fUL << 16U)) | (0x06UL << 16U);
    started_ = startup_wait_ = configured_ = false;
    vbus_seen_ = pullup_on_ = suspended_ = false;
    quiesce_mask_ = quiesce_reset_toggle_mask_ = 0U;
    return;
#else
    set_dplus_pullup(false);
    HAL_NVIC_DisableIRQ(USB_LP_CAN1_RX0_IRQn);
    irq_pending_ = false;
    quiesce_mask_ = quiesce_reset_toggle_mask_ = 0U;
    suspended_ = false;
    cdc_.on_vbus(false);
    if (peripheral_owned_) {
        USB->CNTR = static_cast<std::uint16_t>(USB_CNTR_FRES | USB_CNTR_PDWN);
        RCC->APB1ENR &= ~RCC_APB1ENR_USBEN;
        if (usbpre_saved_) { RCC->CFGR |= RCC_CFGR_USBPRE; }
        else { RCC->CFGR &= ~RCC_CFGR_USBPRE; }
        peripheral_owned_ = false;
    }
    if (pins_owned_) {
        GPIOA->BSRR = static_cast<std::uint32_t>(GPIO_PIN_4 | GPIO_PIN_9) << 16U;
        GPIOA->CRL = (GPIOA->CRL & ~(0x0fUL << 16U)) | (0x04UL << 16U);
        GPIOA->CRH = (GPIOA->CRH & ~(0x0fUL << 4U)) | (0x04UL << 4U);
        pins_owned_ = false;
    }
    started_ = startup_wait_ = configured_ = false;
    quiesce_mask_ = quiesce_reset_toggle_mask_ = 0U;
    suspended_ = false;
    vbus_seen_ = false;
    pullup_on_ = false;
#endif
}

void Stm32UsbDevice::irq_notify() noexcept
{
    if (!started_ || startup_wait_ || faulted_) { return; }
    HAL_NVIC_DisableIRQ(USB_LP_CAN1_RX0_IRQn);
    irq_pending_ = true;
}

void Stm32UsbDevice::poll() noexcept
{
    if (!started_ || faulted_) { return; }
    if (startup_wait_) {
        if (static_cast<std::uint32_t>(DWT->CYCCNT - startup_cycle_) < 72U) { return; }
        USB->CNTR = static_cast<std::uint16_t>(USB_CNTR_RESETM | USB_CNTR_SUSPM |
            USB_CNTR_WKUPM | USB_CNTR_ERRM | USB_CNTR_PMAOVRM | USB_CNTR_CTRM);
        USB->DADDR = USB_DADDR_EF;
        service_reset();
        startup_wait_ = false;
        HAL_NVIC_ClearPendingIRQ(USB_LP_CAN1_RX0_IRQn);
        HAL_NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn);
    }
#if !DSO_USB_STOCK_BENCH_TRIAL
    const bool bus = vbus_present();
    if (bus != vbus_seen_) {
        vbus_seen_ = bus;
        if (!bus) {
            set_dplus_pullup(false);
            quiesce_mask_ = quiesce_reset_toggle_mask_ = 0U;
            suspended_ = false;
        }
        cdc_.on_vbus(bus);
        if (!bus) { USB->DADDR = USB_DADDR_EF; }
    }
    if (bus && !pullup_on_) { set_dplus_pullup(true); }
#endif
    const auto pending_flags = static_cast<std::uint16_t>(USB->ISTR & UsbEvents);
    if (irq_pending_ || pending_flags != 0U) {
        HAL_NVIC_DisableIRQ(USB_LP_CAN1_RX0_IRQn);
        irq_pending_ = false;
        for (std::uint8_t i{}; i < 2U && !faulted_ && service_event(); ++i) { }
        irq_pending_ = false;
        if (!faulted_) { HAL_NVIC_EnableIRQ(USB_LP_CAN1_RX0_IRQn); }
    }
    finish_quiesce();
    cdc_.poll();
}

bool Stm32UsbDevice::send_in(void* context, std::uint8_t ep,
                             const std::uint8_t* data, std::uint8_t size) noexcept
{ return static_cast<Stm32UsbDevice*>(context)->submit_in(ep, data, size); }

bool Stm32UsbDevice::submit_in(std::uint8_t ep, const std::uint8_t* data,
                               std::uint8_t size) noexcept
{
    if (!started_ || startup_wait_ || faulted_ || reset_pending() || ep > 2U || size > 64U ||
        (size != 0U && data == nullptr) || tx_busy_[ep] ||
        (ep != 0U && (quiesce_mask_ & quiesce_bit(ep, true)) != 0U) ||
        ((*ep_register(ep) & USB_EP_CTR_TX) != 0U) ||
        (ep != 0U && !configured_) || (ep == 2U && size > 16U)) { return false; }
    const std::uint16_t pma = ep == 0U ? Ep0TxPma : (ep == 1U ? Ep1TxPma : Ep2TxPma);
    pma_write(pma, data, size);
    btable_write(ep, 1U, size);
    tx_busy_[ep] = true;
    set_ep_status(ep, true, EpStatusValid);
    return true;
}

void Stm32UsbDevice::set_rx_enabled(void* context, std::uint8_t ep, bool enabled) noexcept
{ static_cast<Stm32UsbDevice*>(context)->set_ep_status(ep, false, enabled ? EpStatusValid : EpStatusNak); }

void Stm32UsbDevice::set_halt(void* context, std::uint8_t address, bool halted) noexcept
{
    auto& self = *static_cast<Stm32UsbDevice*>(context);
    const auto ep = static_cast<std::uint8_t>(address & 0x0fU);
    if (ep > 2U || self.faulted_ || reset_pending()) { return; }
    const auto in = (address & 0x80U) != 0U;
    if (ep == 0U) {
        self.set_ep_status(ep, in, halted ? EpStatusStall :
            (in ? EpStatusNak : EpStatusValid));
        return;
    }
    const auto final_status = halted ? EpStatusStall : (in ? EpStatusNak :
        (ep == 1U && self.configured_ ? EpStatusValid : EpStatusDisabled));
    self.quiesce_endpoint(ep, in, final_status, !halted);
}

void Stm32UsbDevice::set_address(void* context, std::uint8_t address) noexcept
{
    const auto& self = *static_cast<Stm32UsbDevice*>(context);
    if (!self.faulted_ && !reset_pending()) {
        USB->DADDR = static_cast<std::uint16_t>(USB_DADDR_EF | (address & 0x7fU));
    }
}

void Stm32UsbDevice::set_configuration(void* context, bool enabled) noexcept
{
    static_cast<Stm32UsbDevice*>(context)->configure_endpoints(enabled);
}

void Stm32UsbDevice::abort_control(void* context) noexcept
{
    auto& self = *static_cast<Stm32UsbDevice*>(context);
    self.tx_busy_[0] = false;
    self.set_ep_status(0U, true, EpStatusNak);
    self.set_ep_status(0U, false, EpStatusValid);
}

void Stm32UsbDevice::cancel_bulk(void* context) noexcept
{
    auto& self = *static_cast<Stm32UsbDevice*>(context);
    if (self.faulted_ || reset_pending()) { return; }
    auto* reg = ep_register(1U);
    const auto current = *reg;
    const auto rx_current = static_cast<std::uint8_t>((current & USB_EPRX_STAT) >> USB_EPRX_STAT_Pos);
    const auto tx_current = static_cast<std::uint8_t>((current & USB_EPTX_STAT) >> USB_EPTX_STAT_Pos);
    const auto rx_index = quiesce_index(1U, false);
    const auto tx_index = quiesce_index(1U, true);
    const auto rx_bit = quiesce_bit(1U, false);
    const auto tx_bit = quiesce_bit(1U, true);
    const auto rx_final = ((self.quiesce_mask_ & rx_bit) != 0U && self.quiesce_target_[rx_index] == EpStatusStall) ||
        rx_current == EpStatusStall ? EpStatusStall :
        (self.configured_ ? EpStatusValid : EpStatusDisabled);
    const auto tx_final = ((self.quiesce_mask_ & tx_bit) != 0U && self.quiesce_target_[tx_index] == EpStatusStall) ||
        tx_current == EpStatusStall ? EpStatusStall : EpStatusNak;
    self.quiesce_endpoint(1U, false, rx_final, false);
    self.quiesce_endpoint(1U, true, tx_final, false);
}

void Stm32UsbDevice::set_ep_status(std::uint8_t ep, bool tx, std::uint8_t status) noexcept
{
    if (ep > 2U || faulted_ || reset_pending()) { return; }
    auto* reg = ep_register(ep);
    for (std::uint8_t attempt{}; attempt < 2U; ++attempt) {
        const auto current = *reg;
        const auto stable = static_cast<std::uint16_t>(current & EpStableMask);
        const auto rx_status = static_cast<std::uint8_t>((current & USB_EPRX_STAT) >> USB_EPRX_STAT_Pos);
        const auto tx_status = static_cast<std::uint8_t>((current & USB_EPTX_STAT) >> USB_EPTX_STAT_Pos);
        const auto actual = tx ? tx_status : rx_status;
        const auto ctr = tx ? USB_EP_CTR_TX : USB_EP_CTR_RX;
        if (actual == status) { return; }
        // An ACK that raced an attempt is accepted work. Never undo it by
        // rearming over its frozen CTR indication.
        if (status == EpStatusValid && (current & ctr) != 0U) { return; }
        if (reset_pending()) { return; }
        const auto write = tx ? detail::usb_ep_compose_write(current, stable, rx_status, status,
                false, false, false, false) :
            detail::usb_ep_compose_write(current, stable, status, tx_status,
                false, false, false, false);
        *reg = write;
    }
    if (reset_pending()) { return; }
    const auto verified = *reg;
    const auto actual = tx ? static_cast<std::uint8_t>((verified & USB_EPTX_STAT) >> USB_EPTX_STAT_Pos) :
        static_cast<std::uint8_t>((verified & USB_EPRX_STAT) >> USB_EPRX_STAT_Pos);
    if (!reset_pending() && actual != status && !(status == EpStatusValid &&
        (verified & (tx ? USB_EP_CTR_TX : USB_EP_CTR_RX)) != 0U)) { fail_closed(); }
}

void Stm32UsbDevice::quiesce_endpoint(std::uint8_t ep, bool tx,
                                     std::uint8_t final_status, bool reset_toggle) noexcept
{
    if (ep == 0U || ep > 2U || faulted_) { return; }
    const auto bit_index = quiesce_index(ep, tx);
    const auto bit = quiesce_bit(ep, tx);
    set_ep_status(ep, tx, EpStatusNak);
    if (faulted_ || reset_pending()) { return; }
    // Restart the single bounded barrier whenever another direction joins it.
    // Extending an existing direction is safe; expiring a new one early is not.
    quiesce_cycle_ = DWT->CYCCNT;
    quiesce_mask_ = static_cast<std::uint8_t>(quiesce_mask_ | bit);
    quiesce_target_[bit_index] = final_status;
    if (reset_toggle) { quiesce_reset_toggle_mask_ = static_cast<std::uint8_t>(quiesce_reset_toggle_mask_ | bit); }
    else { quiesce_reset_toggle_mask_ = static_cast<std::uint8_t>(quiesce_reset_toggle_mask_ & ~bit); }
    if (tx) { tx_busy_[ep] = true; }
}

void Stm32UsbDevice::finish_quiesce() noexcept
{
    if (quiesce_mask_ == 0U || suspended_ || faulted_ || reset_pending() ||
        static_cast<std::uint32_t>(DWT->CYCCNT - quiesce_cycle_) < 72000U) { return; }
    for (std::uint8_t bit_index{}; bit_index < 4U && !faulted_; ++bit_index) {
        if (reset_pending()) { return; }
        const auto bit = static_cast<std::uint8_t>(1U << bit_index);
        if ((quiesce_mask_ & bit) == 0U) { continue; }
        const auto ep = static_cast<std::uint8_t>(1U + (bit_index & 1U));
        const bool tx = bit_index >= 2U;
        auto* reg = ep_register(ep);
        auto current = *reg;
        const auto stable = static_cast<std::uint16_t>(current & EpStableMask);
        const auto rx = static_cast<std::uint8_t>((current & USB_EPRX_STAT) >> USB_EPRX_STAT_Pos);
        const auto txs = static_cast<std::uint8_t>((current & USB_EPTX_STAT) >> USB_EPTX_STAT_Pos);
        const auto pending = tx ? USB_EP_CTR_TX : USB_EP_CTR_RX;
        if ((current & pending) != 0U) {
            if (reset_pending()) { return; }
            *reg = detail::usb_ep_compose_write(current, stable, rx, txs,
                !tx, tx, false, false);
            if (reset_pending()) { return; }
            if (!tx && ep == 1U) { btable_write(1U, 3U, BtableRx64); }
            current = *reg;
        }
        if (reset_pending()) { return; }
        if ((current & pending) != 0U) { fail_closed(); break; }
        if ((quiesce_reset_toggle_mask_ & bit) != 0U) { reset_ep_toggle(ep, tx); }
        set_ep_status(ep, tx, quiesce_target_[bit_index]);
        if (tx) { tx_busy_[ep] = false; }
        quiesce_mask_ = static_cast<std::uint8_t>(quiesce_mask_ & ~bit);
        quiesce_reset_toggle_mask_ = static_cast<std::uint8_t>(quiesce_reset_toggle_mask_ & ~bit);
    }
}

void Stm32UsbDevice::reset_ep_toggle(std::uint8_t ep, bool transmit) noexcept
{
    if (ep > 2U || faulted_ || reset_pending()) { return; }
    auto* reg = ep_register(ep);
    const auto current = *reg;
    const auto stable = static_cast<std::uint16_t>(current & EpStableMask);
    const auto rx = static_cast<std::uint8_t>((current & USB_EPRX_STAT) >> USB_EPRX_STAT_Pos);
    const auto tx = static_cast<std::uint8_t>((current & USB_EPTX_STAT) >> USB_EPTX_STAT_Pos);
    if (reset_pending()) { return; }
    *reg = detail::usb_ep_compose_write(current, stable, rx, tx, false, false, !transmit, transmit);
}

void Stm32UsbDevice::configure_endpoints(bool enabled) noexcept
{
    if (faulted_ || reset_pending()) { return; }
    configured_ = enabled;
    if (enabled) {
        USB->BTABLE = BtableBase;
        btable_write(0U, 0U, Ep0TxPma); btable_write(0U, 1U, 0U);
        btable_write(0U, 2U, Ep0RxPma); btable_write(0U, 3U, BtableRx64);
        btable_write(1U, 0U, Ep1TxPma); btable_write(1U, 1U, 0U);
        btable_write(1U, 2U, Ep1RxPma); btable_write(1U, 3U, BtableRx64);
        btable_write(2U, 0U, Ep2TxPma); btable_write(2U, 1U, 0U);
        btable_write(2U, 2U, 0U); btable_write(2U, 3U, 0U);
        const auto ep0 = static_cast<std::uint16_t>(USB_EP_CONTROL | 0U);
        const auto ep1 = static_cast<std::uint16_t>(USB_EP_BULK | 1U);
        const auto ep2 = static_cast<std::uint16_t>(USB_EP_INTERRUPT | 2U);
        const auto old0 = *ep_register(0U);
        if (reset_pending()) { return; }
        *ep_register(0U) = detail::usb_ep_compose_write(old0, ep0, EpStatusValid, EpStatusNak,
            true, true, true, true);
        if (reset_pending()) { return; }
        const auto old1 = *ep_register(1U);
        *ep_register(1U) = detail::usb_ep_compose_write(old1, ep1, EpStatusValid, EpStatusNak,
            true, true, true, true);
        if (reset_pending()) { return; }
        const auto old2 = *ep_register(2U);
        *ep_register(2U) = detail::usb_ep_compose_write(old2, ep2, EpStatusDisabled, EpStatusNak,
            true, true, true, true);
    } else {
        for (std::uint8_t ep{1U}; ep <= 2U; ++ep) {
            auto* reg = ep_register(ep); const auto current = *reg;
            if (reset_pending()) { return; }
            const auto fields = static_cast<std::uint16_t>((current & (USB_EP_KIND | USB_EPADDR_FIELD)) |
                (ep == 1U ? USB_EP_BULK : USB_EP_INTERRUPT));
            *reg = detail::usb_ep_compose_write(current, fields, EpStatusDisabled, EpStatusDisabled,
                true, true, true, true);
            if (reset_pending()) { return; }
            tx_busy_[ep] = false;
        }
    }
    // EP0 remains the control pipe regardless of configuration state.
    set_ep_status(0U, true, EpStatusNak);
    set_ep_status(0U, false, EpStatusValid);
}

void Stm32UsbDevice::service_reset() noexcept
{
    quiesce_mask_ = quiesce_reset_toggle_mask_ = 0U;
    suspended_ = false;
    cdc_.on_reset();
    configured_ = false;
    tx_busy_[0] = tx_busy_[1] = tx_busy_[2] = false;
    USB->DADDR = USB_DADDR_EF;
    USB->BTABLE = BtableBase;
    btable_write(0U, 0U, Ep0TxPma);
    btable_write(0U, 1U, 0U);
    btable_write(0U, 2U, Ep0RxPma);
    btable_write(0U, 3U, BtableRx64);
    const auto current = *ep_register(0U);
    *ep_register(0U) = detail::usb_ep_compose_write(current, USB_EP_CONTROL,
        EpStatusValid, EpStatusNak, true, true, true, true);
}

bool Stm32UsbDevice::service_event() noexcept
{
    const auto flags = static_cast<std::uint16_t>(USB->ISTR & UsbEvents);
    if (flags == 0U) { return false; }
    if ((flags & USB_ISTR_RESET) != 0U) {
        clear_istr(USB_ISTR_RESET); service_reset(); return true;
    }
    if ((flags & USB_ISTR_WKUP) != 0U) {
        clear_istr(USB_ISTR_WKUP); suspended_ = false;
        if (quiesce_mask_ != 0U) { quiesce_cycle_ = DWT->CYCCNT; }
        cdc_.on_resume(); return true;
    }
    if ((flags & USB_ISTR_SUSP) != 0U) {
        clear_istr(USB_ISTR_SUSP); suspended_ = true; cdc_.on_suspend(); return true;
    }
    if ((flags & USB_ISTR_CTR) != 0U) { service_ctr(); return true; }
    if ((flags & USB_ISTR_ERR) != 0U) { clear_istr(USB_ISTR_ERR); return true; }
    if ((flags & USB_ISTR_PMAOVR) != 0U) {
        clear_istr(USB_ISTR_PMAOVR);
        fail_closed();
        return true;
    }
    if ((flags & USB_ISTR_ESOF) != 0U) { clear_istr(USB_ISTR_ESOF); return true; }
    return false;
}

void Stm32UsbDevice::service_ctr() noexcept
{
    if (reset_pending()) { return; }
    const auto istr = USB->ISTR;
    if ((istr & USB_ISTR_RESET) != 0U) { return; }
    const auto ep = static_cast<std::uint8_t>(istr & USB_ISTR_EP_ID);
    const bool out = (istr & USB_ISTR_DIR) != 0U;
    if (ep > 2U) { fail_closed(); return; }
    auto* reg = ep_register(ep);
    const auto current = *reg;
    const auto stable = static_cast<std::uint16_t>(current & EpStableMask);
    auto rx_status = static_cast<std::uint8_t>((current & USB_EPRX_STAT) >> USB_EPRX_STAT_Pos);
    auto tx_status = static_cast<std::uint8_t>((current & USB_EPTX_STAT) >> USB_EPTX_STAT_Pos);
    if (out && (current & USB_EP_CTR_RX) != 0U) {
        if (ep != 0U && (quiesce_mask_ & quiesce_bit(ep, false)) != 0U) {
            if (reset_pending()) { return; }
            *reg = detail::usb_ep_compose_write(current, stable, rx_status, tx_status,
                true, false, false, false);
            if (ep == 1U) { btable_write(1U, 3U, BtableRx64); }
            return;
        }
        const bool setup = ep == 0U && (current & USB_EP_SETUP) != 0U;
        const auto count_word = static_cast<std::uint16_t>(btable_read(ep, 3U) & 0x03ffU);
        const auto count = static_cast<std::uint8_t>(count_word <= 64U ? count_word : 0U);
        std::uint8_t packet[64U]{};
        const auto rx_addr = btable_read(ep, 2U);
        if (count_word <= 64U) { pma_read(rx_addr, packet, count); }
        if (reset_pending()) { return; }
        rx_status = EpStatusNak;
        *reg = detail::usb_ep_compose_write(current, stable, rx_status, tx_status,
            true, setup, false, false);
        if (reset_pending()) { return; }
        if (ep <= 1U) { btable_write(ep, 3U, BtableRx64); }
        if (reset_pending()) { return; }
        if (setup) {
            tx_busy_[0] = false;
            if (count_word == 8U) { cdc_.on_setup(packet); }
            else { set_ep_status(0U, true, EpStatusStall); set_ep_status(0U, false, EpStatusStall); }
        } else if (count_word <= 64U) {
            cdc_.on_out(ep, packet, count);
        } else {
            fail_closed();
        }
        return;
    }
    if (!out && (current & USB_EP_CTR_TX) != 0U) {
        if (ep != 0U && (quiesce_mask_ & quiesce_bit(ep, true)) != 0U) {
            if (reset_pending()) { return; }
            *reg = detail::usb_ep_compose_write(current, stable, rx_status, tx_status,
                false, true, false, false);
            return;
        }
        if (tx_status != EpStatusStall) { tx_status = EpStatusNak; }
        if (reset_pending()) { return; }
        *reg = detail::usb_ep_compose_write(current, stable, rx_status, tx_status,
            false, true, false, false);
        if (reset_pending()) { return; }
        tx_busy_[ep] = false;
        if (reset_pending()) { return; }
        cdc_.on_in_complete(ep);
        return;
    }
    // The USB ISTR direction and endpoint CTR flags must agree; retain the
    // peripheral pending state and fail closed if the register image is bad.
    if (!reset_pending()) { fail_closed(); }
}

void Stm32UsbDevice::fail_closed() noexcept
{
    if (faulted_ || reset_pending()) { return; }
    faulted_ = true;
    quiesce_mask_ = quiesce_reset_toggle_mask_ = 0U;
    set_dplus_pullup(false);
    HAL_NVIC_DisableIRQ(USB_LP_CAN1_RX0_IRQn);
    irq_pending_ = false;
    cdc_.on_reset();
    if (peripheral_owned_) { USB->CNTR = static_cast<std::uint16_t>(USB_CNTR_FRES | USB_CNTR_PDWN); }
}

} // namespace dso
