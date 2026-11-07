#include "dso_stm32_display.hpp"
#include "dso_stm32_shared_guard.hpp"
#include "dso_u32_counter.hpp"
#include "dso_board_bringup.hpp"
#if DSO_BOARD_BRINGUP
#include "dso_stm32_lcd_dma.hpp"
#endif
#include "dso_stm32_board_clock.hpp"
#include "main.h"

namespace dso {
namespace {

constexpr std::uint32_t DataMask{0x00ffU};
constexpr std::uint32_t Read{0x0400U}, Reset{0x0800U};
constexpr std::uint32_t Select{0x2000U}, Data{0x4000U}, Write{0x8000U};
constexpr std::uint32_t OwnedB{DataMask | Read | Reset};
constexpr std::uint32_t OwnedC{Select | Data | Write};
constexpr std::uint32_t GpioClocks{RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPCEN};
constexpr std::uint32_t PhaseCycles{36U}; // >=500ns at the required 72MHz HCLK.
constexpr std::uint32_t MillisecondCycles{72000U};
constexpr unsigned PhaseIterations{4096U}, MillisecondIterations{100000U};

struct Binding final {
    Stm32DisplayState state{Stm32DisplayState::Dormant};
    Stm32DisplayFault fault{Stm32DisplayFault::None};
    bool owned{};
    bool selected{};
    bool reset_asserted{};
    bool dma_enabled{};
    bool dma_pending{};
};
Binding binding;
static_assert(sizeof(Binding) <= 8U, "Keep the dormant bus state bounded");


bool clocks_match() noexcept
{
    return detail::lcd_clock_tree_matches();
}

bool pins_unclaimed() noexcept
{
    // BKP registers must not be read with their interface clock disabled. No
    // backup-domain reset, DBP unlock, LSE change or MAPR read/write is needed.
    return (RCC->APB1ENR & RCC_APB1ENR_BKPEN) != 0U &&
        // LSERDY can remain high for six LSE cycles after LSEON is cleared.
        // Do not claim PC14/15 during that documented stopping transient.
        (RCC->BDCR & (RCC_BDCR_LSEON | RCC_BDCR_LSERDY | RCC_BDCR_LSEBYP)) == 0U &&
        (BKP->CR & BKP_CR_TPE) == 0U &&
        (BKP->RTCCR & (BKP_RTCCR_CCO | BKP_RTCCR_ASOE)) == 0U &&
        (DBGMCU->CR & DBGMCU_CR_TRACE_IOEN) == 0U;
}


bool gpio_clocks() noexcept
{
    return (RCC->APB2ENR & GpioClocks) == GpioClocks &&
        (RCC->APB2RSTR & (RCC_APB2RSTR_IOPBRST | RCC_APB2RSTR_IOPCRST)) == 0U;
}

bool configuration_matches() noexcept
{
    // All data pins are in CRL. PB8/9 and PB12..15 share CRH but are not ours.
    return GPIOB->CRL == 0x22222222U &&
        (GPIOB->CRH & 0x0000ff00U) == 0x00002200U &&
        (GPIOC->CRH & 0xfff00000U) == 0x22200000U;
}

bool idle_levels_match() noexcept
{
    const auto expected_b = Read | (binding.reset_asserted ? 0U : Reset);
    const auto expected_c = Write | (binding.selected ? 0U : Select);
    return (GPIOB->ODR & (Read | Reset)) == expected_b &&
        (GPIOC->ODR & (Select | Write)) == expected_c;
}

void release_if_owned() noexcept
{
    // Cleanup is best effort, never a reason to drive a reallocated/clock-off
    // port. RESET is deliberately preserved, including a failed reset pulse.
    if (binding.owned && gpio_clocks() && pins_unclaimed() && configuration_matches()) {
        GPIOC->BSRR = Select | Write;
        GPIOB->BSRR = Read;
        __DSB();
    }
    binding.selected = false;
}

bool fail(Stm32DisplayFault reason) noexcept
{
#if DSO_BOARD_BRINGUP
    if (binding.dma_enabled) { stm32_lcd_dma_abort(); }
#endif
    binding.dma_pending = false;
    if (binding.state != Stm32DisplayState::Fault) {
        binding.fault = reason;
        release_if_owned();
        binding.state = Stm32DisplayState::Fault;
    }
    return false;
}

bool wait_cycles(std::uint32_t cycles, unsigned iterations) noexcept
{
    const auto start = static_cast<std::uint32_t>(DWT->CYCCNT);
    for (unsigned i{}; i < iterations; ++i) {
        const auto elapsed = static_cast<std::uint32_t>(DWT->CYCCNT - start);
        if (elapsed >= counter_contract::U32HalfRange) { return false; }
        if (elapsed >= cycles) { return true; }
        __NOP();
    }
    return false; // Also bounds a missing/stopped cycle counter.
}

bool ready(void* context) noexcept
{
    if (context != &binding || binding.state != Stm32DisplayState::Prepared) { return false; }
    if (binding.dma_pending) { return fail(Stm32DisplayFault::Protocol); }
    if (!stm32_foreground()) { return fail(Stm32DisplayFault::Execution); }
    if (!clocks_match() || !gpio_clocks()) { return fail(Stm32DisplayFault::Clocks); }
    if (!pins_unclaimed()) { return fail(Stm32DisplayFault::Ownership); }
    if (!configuration_matches() || !idle_levels_match()) { return fail(Stm32DisplayFault::Configuration); }
    if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0U ||
        (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U) { return fail(Stm32DisplayFault::Timing); }
    return true;
}

bool phase() noexcept
{
    return wait_cycles(PhaseCycles, PhaseIterations) || fail(Stm32DisplayFault::Timing);
}

bool select(void* context, bool asserted) noexcept
{
    if (!ready(context)) { return false; }
    if (asserted && (binding.selected || binding.reset_asserted)) { return fail(Stm32DisplayFault::Protocol); }
    GPIOC->BSRR = asserted ? Select << 16U : Select;
    __DSB();
    binding.selected = asserted;
    return (GPIOC->ODR & Select) == (asserted ? 0U : Select) ? phase() : fail(Stm32DisplayFault::Configuration);
}

bool reset(void* context, bool asserted) noexcept
{
    if (!ready(context)) { return false; }
    if (binding.selected) { return fail(Stm32DisplayFault::Protocol); }
    if (!stm32_stopped_acquisition()) { return fail(Stm32DisplayFault::Ownership); }
    GPIOB->BSRR = asserted ? Reset << 16U : Reset;
    __DSB();
    binding.reset_asserted = asserted;
    return (GPIOB->ODR & Reset) == (asserted ? 0U : Reset) ? phase() : fail(Stm32DisplayFault::Configuration);
}

bool strobe() noexcept
{
    if (!phase()) { return false; }
    GPIOC->BSRR = Write << 16U;
    __DSB();
    if ((GPIOC->ODR & Write) != 0U) { return fail(Stm32DisplayFault::Configuration); }
    if (!phase()) { return false; }
    GPIOC->BSRR = Write;
    __DSB();
    if ((GPIOC->ODR & Write) == 0U) { return fail(Stm32DisplayFault::Configuration); }
    return phase();
}

bool emit_byte(bool data, std::uint8_t value) noexcept
{
    // Both command paths acquire the selected bus before calling this leaf.
    // Atomic masked stores preserve PWM/buttons; every byte retains data/DC,
    // RD/RESET/CS/WR latch checks and the same setup/low/high strobe phases.
    GPIOB->BSRR = std::uint32_t{value} | ((~std::uint32_t{value} & DataMask) << 16U);
    GPIOC->BSRR = data ? Data : Data << 16U;
    __DSB();
    if ((GPIOB->ODR & (DataMask | Read | Reset)) != (std::uint32_t{value} | Read | Reset) ||
        (GPIOC->ODR & (Data | Select | Write)) != (Write | (data ? Data : 0U))) {
        return fail(Stm32DisplayFault::Configuration);
    }
    return strobe();
}

bool write(void* context, bool data, std::uint8_t value) noexcept
{
    if (!ready(context)) { return false; }
    if (!binding.selected || binding.reset_asserted) { return fail(Stm32DisplayFault::Protocol); }
    return emit_byte(data,value);
}

bool write_sequence(void* context, const std::uint8_t* bytes, std::uint16_t count,
                     std::uint16_t data_bits) noexcept
{
    if (bytes == nullptr || count == 0U || count > 11U ||
        (std::uint32_t{data_bits} >> count) != 0U) { return false; }
    if (!ready(context)) { return false; }
    if (!binding.selected || binding.reset_asserted) { return fail(Stm32DisplayFault::Protocol); }
    // The portable protocol owner supplies command bytes and D/CX bits. No
    // ILI9341 opcode/address interpretation is duplicated in this GPIO adapter.
    for (std::uint16_t index{}; index < count; ++index) {
        if (!emit_byte((data_bits & (1U << index)) != 0U,bytes[index])) { return false; }
    }
    return ready(context);
}

bool pixels(void* context, const std::uint16_t* source, std::uint16_t count,
             bool repeat) noexcept
{
    if (source == nullptr || count == 0U || count > DisplayBurstPixels) { return false; }
    if (!ready(context)) { return false; }
    if (!binding.selected || binding.reset_asserted) { return fail(Stm32DisplayFault::Protocol); }
    // One exclusive foreground transaction owns the pins/clocks throughout
    // this bounded burst. IRQ handlers never change LCD GPIO. Revalidate all
    // ownership/mode/clock guards at both boundaries; preserve each byte's
    // data/control readback and the same 500ns setup/low/high timing policy.
    GPIOC->BSRR = Data;
    __DSB();
    for (std::uint16_t index{}; index < count; ++index) {
        const auto value = source[repeat ? 0U : index];
        for (unsigned byte{}; byte < 2U; ++byte) {
            const auto data = static_cast<std::uint8_t>(byte == 0U ? value >> 8U : value);
            GPIOB->BSRR = std::uint32_t{data} | ((~std::uint32_t{data} & DataMask) << 16U);
            __DSB();
            if ((GPIOB->ODR & (DataMask | Read | Reset)) != (std::uint32_t{data} | Read | Reset) ||
                (GPIOC->ODR & (Data | Select | Write)) != (Data | Write)) {
                return fail(Stm32DisplayFault::Configuration);
            }
            if (!strobe()) { return false; }
        }
    }
    return ready(context);
}

bool delay_ms(void* context, std::uint32_t duration) noexcept
{
    if (!ready(context)) { return false; }
    if (binding.selected || duration > 120U) { return fail(Stm32DisplayFault::Protocol); }
    if (!stm32_stopped_acquisition()) { return fail(Stm32DisplayFault::Ownership); }
    for (std::uint32_t i{}; i < duration; ++i) {
        if (!wait_cycles(MillisecondCycles, MillisecondIterations)) { return fail(Stm32DisplayFault::Timing); }
    }
    return true;
}

#if DSO_BOARD_BRINGUP
bool start_pixels(void* context, const std::uint16_t* source, std::uint16_t count) noexcept
{
    if (!ready(context) || !binding.selected || binding.reset_asserted) { return false; }
    if (!binding.dma_enabled) { return pixels(context, source, count, false); }
    GPIOC->BSRR = Data;
    __DSB();
    if ((GPIOC->ODR & (Data | Select | Write)) != (Data | Write)) {
        return fail(Stm32DisplayFault::Configuration);
    }
    if (!stm32_lcd_dma_start(source, count)) { return fail(Stm32DisplayFault::Timing); }
    binding.dma_pending = true;
    return true;
}

TransferState poll_pixels(void* context) noexcept
{
    if (context != &binding || binding.state != Stm32DisplayState::Prepared) { return TransferState::Fault; }
    if (!binding.dma_pending) { return TransferState::Complete; }
    const auto state = stm32_lcd_dma_poll();
    if (state == TransferState::Pending) { return state; }
    binding.dma_pending = false;
    if (state != TransferState::Complete || !ready(context)) {
        static_cast<void>(fail(Stm32DisplayFault::Timing));
        return TransferState::Fault;
    }
    return TransferState::Complete;
}

void abort_pixels(void* context) noexcept
{
    if (context != &binding) { return; }
    if (binding.dma_enabled) { stm32_lcd_dma_abort(); }
    binding.dma_pending = false;
    // Cancellation may stop between the falling/rising WR requests. Finish a
    // bounded low hold before restoring high, then its high hold before CS can
    // be released. The invalidated screen will repaint any partial pixel.
    if (binding.owned && gpio_clocks() && pins_unclaimed() && configuration_matches()) {
        if (!phase()) { return; }
        GPIOC->BSRR = Write;
        __DSB();
        static_cast<void>(phase());
    }
}
#endif

bool locked(GPIO_TypeDef* gpio, std::uint32_t mask) noexcept
{
    const auto value = static_cast<std::uint32_t>(gpio->LCKR);
    return (value & GPIO_LCKR_LCKK) != 0U && (value & mask) != 0U;
}
} // namespace

bool prepare_stm32_display(Stm32DisplayQualification value) noexcept
{
#if DSO_BOARD_BRINGUP
    // Asynchronous ownership must end before revocation or reinitialization.
    if (binding.dma_enabled) { stm32_lcd_dma_abort(); }
    binding.dma_pending = false;
    binding.dma_enabled = false;
#endif
    if (!value.pinmap_verified || !value.supply_verified || !value.gpio_electrical_verified ||
        !value.no_jtag_or_swo_verified) {
        binding = Binding{};
        binding.fault = Stm32DisplayFault::Qualification;
        return false;
    }
    if (binding.state == Stm32DisplayState::Prepared && binding.selected) { return fail(Stm32DisplayFault::Protocol); }
    // Full, explicit preparation may recover a latched failure. No previous
    // DisplayStatus is revived: caller must reinitialize the LCD driver too.
    binding.state = Stm32DisplayState::Dormant;
    binding.fault = Stm32DisplayFault::None;
    if (!stm32_foreground()) { return fail(Stm32DisplayFault::Execution); }
    if (!clocks_match()) { return fail(Stm32DisplayFault::Clocks); }
    if (!pins_unclaimed() || !stm32_stopped_acquisition()) { return fail(Stm32DisplayFault::Ownership); }
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0U ||
        (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U ||
        !wait_cycles(PhaseCycles, PhaseIterations)) { return fail(Stm32DisplayFault::Timing); }
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    if (!gpio_clocks()) { return fail(Stm32DisplayFault::Clocks); }
    if (locked(GPIOB, OwnedB) || locked(GPIOC, OwnedC)) { return fail(Stm32DisplayFault::Ownership); }
    // Set inactive control levels before any pin becomes an output. RD is
    // never pulsed; PB0..7 never change direction in this write-only adapter.
    GPIOC->BSRR = Select | Write | (Data << 16U);
    GPIOB->BSRR = Read | Reset | (DataMask << 16U);
    __DSB();
    GPIO_InitTypeDef config{};
    config.Pin = OwnedC;
    config.Mode = GPIO_MODE_OUTPUT_PP;
    config.Pull = GPIO_NOPULL;
    config.Speed = GPIO_SPEED_FREQ_LOW; // F1 HAL: 2MHz MODE, not 50MHz.
    HAL_GPIO_Init(GPIOC, &config);
    // Establish CS/WR inactive outputs before driving the data/reset bus. A
    // partial control-mode failure must not proceed into data-pin activation.
    if ((GPIOC->CRH & 0xfff00000U) != 0x22200000U ||
        (GPIOC->ODR & (Select | Write)) != (Select | Write)) {
        return fail(Stm32DisplayFault::Configuration);
    }
    if (!phase()) { return false; } // Explicit CS-deassert settle before data outputs.
    config.Pin = OwnedB;
    HAL_GPIO_Init(GPIOB, &config);
    binding.owned = true;
    binding.selected = false;
    binding.reset_asserted = false;
    if (!configuration_matches() || !idle_levels_match()) { return fail(Stm32DisplayFault::Configuration); }
    binding.state = Stm32DisplayState::Prepared;
    return true;
}

DisplayBus stm32_display_bus() noexcept
{
#if DSO_BOARD_BRINGUP
    return {&binding, select, reset, write, delay_ms, pixels, start_pixels, poll_pixels, abort_pixels, write_sequence};
#else
    return {&binding, select, reset, write, delay_ms, pixels, nullptr, nullptr, nullptr, write_sequence};
#endif
}

bool enable_stm32_display_dma() noexcept
{
#if DSO_BOARD_BRINGUP
    if (!ready(&binding) || binding.selected || binding.reset_asserted || !stm32_stopped_acquisition()) { return false; }
    binding.dma_enabled = prepare_stm32_lcd_dma(true);
    return binding.dma_enabled;
#else
    return false;
#endif
}

Stm32DisplayState stm32_display_state() noexcept { return binding.state; }
Stm32DisplayFault stm32_display_fault() noexcept { return binding.fault; }
} // namespace dso
