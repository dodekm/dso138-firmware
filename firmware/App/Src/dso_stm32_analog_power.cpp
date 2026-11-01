#include "dso_stm32_analog_power.hpp"
#include "dso_stm32_shared_guard.hpp"
#include "main.h"

namespace dso {
namespace {

static_assert(USE_HAL_TIM_REGISTER_CALLBACKS == 0U,
    "Local TIM handle requires disabled callback registration and a non-retaining PWM MSP hook");

constexpr std::uint32_t Pins{0x0300U};
constexpr std::uint32_t ModeMask{0x000000ffU};
constexpr std::uint32_t GpioLowMode{0x00000022U};
constexpr std::uint32_t AlternateMode{0x000000aaU};
constexpr std::uint32_t Period{4095U};
constexpr std::uint32_t Enables{TIM_CCER_CC3E | TIM_CCER_CC4E};
constexpr std::uint32_t PwmModes{TIM_OCMODE_PWM1 | (TIM_OCMODE_PWM1 << 8U) |
    TIM_CCMR2_OC3PE | TIM_CCMR2_OC4PE};
constexpr std::uint32_t HalfRange{0x80000000U};
constexpr unsigned ProgressIterations{4096U};
constexpr std::uint32_t ProgressDeadlineCycles{72000U};

struct Binding final {
    std::uint16_t generator{};
    std::uint16_t reference{};
    Stm32AnalogPowerState state{Stm32AnalogPowerState::Unqualified};
    Stm32AnalogPowerFault fault{Stm32AnalogPowerFault::None};
    bool qualified{};
    bool timer_owned{};
    bool may_be_active{};
};
Binding binding;
static_assert(sizeof(Binding) <= 16U, "Keep analog-power state within the C8 SRAM budget");

bool qualified(Stm32AnalogPowerQualification q) noexcept
{
    return q.pinmap_verified && q.input_supply_verified && q.switching_profile_verified &&
        q.start_stop_sequence_verified;
}

bool revoke() noexcept
{
    binding.qualified = false;
    binding.state = Stm32AnalogPowerState::Unqualified;
    binding.fault = Stm32AnalogPowerFault::Qualification;
    // Keep the profile/ownership history. This is not an electrical shutdown.
    return false;
}


bool clocks_match() noexcept
{
    if (HAL_RCC_GetSysClockFreq() != 72000000U || HAL_RCC_GetHCLKFreq() != 72000000U) { return false; }
    RCC_ClkInitTypeDef clocks{};
    std::uint32_t latency{};
    HAL_RCC_GetClockConfig(&clocks, &latency);
    const auto pclk1 = HAL_RCC_GetPCLK1Freq();
    return pclk1 <= 36000000U && pclk1 *
        (clocks.APB1CLKDivider == RCC_HCLK_DIV1 ? 1U : 2U) == 72000000U;
}


bool pin_routing_available() noexcept
{
    // Only the reliable TIM4 remap field is inspected. SWJ_CFG reads are
    // undefined; neither JTAG routing nor any unrelated MAPR bit is written.
    return (RCC->APB2ENR & RCC_APB2ENR_AFIOEN) != 0U &&
        (AFIO->MAPR & AFIO_MAPR_TIM4_REMAP) == 0U &&
        (DBGMCU->CR & DBGMCU_CR_DBG_TIM4_STOP) == 0U;
}

bool peripheral_clocks() noexcept
{
    return (RCC->APB2ENR & RCC_APB2ENR_IOPBEN) != 0U &&
        (RCC->APB1ENR & RCC_APB1ENR_TIM4EN) != 0U &&
        (RCC->APB2RSTR & RCC_APB2RSTR_IOPBRST) == 0U &&
        (RCC->APB1RSTR & RCC_APB1RSTR_TIM4RST) == 0U;
}

bool pins_locked() noexcept
{
    const auto lock = static_cast<std::uint32_t>(GPIOB->LCKR);
    return (lock & GPIO_LCKR_LCKK) != 0U && (lock & Pins) != 0U;
}

bool gpio_low() noexcept
{
    return (GPIOB->CRH & ModeMask) == GpioLowMode && (GPIOB->ODR & Pins) == 0U;
}

bool gpio_alternate() noexcept
{
    return (GPIOB->CRH & ModeMask) == AlternateMode && (GPIOB->ODR & Pins) == 0U;
}

bool pins_disconnected_for_start() noexcept
{
    // TIM4 CH1/CH2 also reach LCD nets PB6/PB7. A previously clock-gated timer
    // must not resume a retained channel onto either alternate-function pin.
    const auto lcd_modes = static_cast<std::uint32_t>(GPIOB->CRL) >> 24U;
    const auto sixth = lcd_modes & 15U;
    const auto seventh = lcd_modes >> 4U;
    if (((sixth & 3U) != 0U && (sixth & 8U) != 0U) ||
        ((seventh & 3U) != 0U && (seventh & 8U) != 0U)) { return false; }
    const auto modes = static_cast<std::uint32_t>(GPIOB->CRH) & ModeMask;
    const auto low = static_cast<std::uint32_t>(GPIOB->ODR) & Pins;
    const auto first = modes & 15U;
    const auto second = modes >> 4U;
    const bool first_input = first == 0U || first == 4U || first == 8U;
    const bool second_input = second == 0U || second == 4U || second == 8U;
    return (first_input || (first == 2U && (low & 0x0100U) == 0U)) &&
        (second_input || (second == 2U && (low & 0x0200U) == 0U));
}

bool pins_owned_for_shutdown() noexcept
{
    const auto modes = static_cast<std::uint32_t>(GPIOB->CRH) & ModeMask;
    const auto first = modes & 15U;
    const auto second = modes >> 4U;
    return (first == 2U || first == 10U) && (second == 2U || second == 10U) &&
        (GPIOB->ODR & Pins) == 0U;
}

bool timer_idle() noexcept
{
    return (TIM4->CR1 & TIM_CR1_CEN) == 0U && TIM4->CCER == 0U &&
        TIM4->DIER == 0U && TIM4->SMCR == 0U;
}

bool timer_matches(bool running) noexcept
{
    return TIM4->CR1 == (TIM_CR1_ARPE | (running ? TIM_CR1_CEN : 0U)) &&
        TIM4->CR2 == 0U && TIM4->SMCR == 0U && TIM4->DIER == 0U &&
        TIM4->CCMR1 == 0U && TIM4->CCMR2 == PwmModes &&
        TIM4->CCER == (running ? Enables : 0U) && TIM4->PSC == 0U &&
        TIM4->ARR == Period && TIM4->CCR1 == 0U && TIM4->CCR2 == 0U &&
        TIM4->CCR3 == binding.reference && TIM4->CCR4 == binding.generator && TIM4->DCR == 0U;
}

bool timer_owned_for_shutdown() noexcept
{
    // A failed handoff/stop can leave either channel disabled or CEN clear.
    // Never re-enable them to manufacture a normal running state for recovery.
    return (TIM4->CR1 & ~TIM_CR1_CEN) == TIM_CR1_ARPE &&
        TIM4->CR2 == 0U && TIM4->SMCR == 0U && TIM4->DIER == 0U &&
        TIM4->CCMR1 == 0U && TIM4->CCMR2 == PwmModes &&
        (TIM4->CCER & ~Enables) == 0U && TIM4->PSC == 0U && TIM4->ARR == Period &&
        TIM4->CCR1 == 0U && TIM4->CCR2 == 0U && TIM4->CCR3 <= Period &&
        TIM4->CCR4 == binding.generator && TIM4->DCR == 0U;
}

void stop_disconnected_timer() noexcept
{
    // This cleanup is allowed only BEFORE alternate-function handoff, while
    // both verified GPIO outputs still hold the externally qualified low level.
    // Once handoff may have occurred, no automatic carrier change is made.
    if (binding.timer_owned && !binding.may_be_active && peripheral_clocks() && gpio_low()) {
        CLEAR_BIT(TIM4->CCER, Enables);
        CLEAR_BIT(TIM4->CR1, TIM_CR1_CEN);
        __DSB();
    }
}

bool fail(Stm32AnalogPowerFault fault) noexcept
{
    if (binding.state != Stm32AnalogPowerState::Fault) {
        binding.fault = fault;
        stop_disconnected_timer();
        binding.qualified = false;
        binding.state = Stm32AnalogPowerState::Fault;
    }
    return false;
}

bool progressing_counter() noexcept
{
    if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0U ||
        (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U) { return false; }
    const auto start_cycles = static_cast<std::uint32_t>(DWT->CYCCNT);
    auto previous_count = static_cast<std::uint32_t>(TIM4->CNT);
    std::uint32_t previous_elapsed{};
    if (previous_count > Period) { return false; }
    for (unsigned i{}; i < ProgressIterations; ++i) {
        const auto current = static_cast<std::uint32_t>(TIM4->CNT);
        const auto elapsed = static_cast<std::uint32_t>(DWT->CYCCNT - start_cycles);
        if (current > Period || elapsed < previous_elapsed ||
            elapsed >= HalfRange || elapsed >= ProgressDeadlineCycles) { return false; }
        // Under sole timer ownership, no CNT writes occur while running.
        // A decrease therefore observes a natural overflow/update, proving
        // that ARR/CCR preloads have crossed a hardware update boundary even
        // if an earlier software UG was lost. Readback alone sees preloads.
        if (elapsed >= 72U && current < previous_count) { return true; }
        previous_count = current;
        previous_elapsed = elapsed;
        __NOP();
    }
    return false;
}

void configure_gpio(std::uint32_t mode) noexcept
{
    GPIO_InitTypeDef pins{};
    pins.Pin = Pins;
    pins.Mode = mode;
    pins.Pull = GPIO_NOPULL;
    pins.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &pins);
}

bool start(const Stm32AnalogPowerProfile& profile, Stm32AnalogPowerQualification q) noexcept
{
    if (!qualified(q)) { return revoke(); }
    const auto generator = profile.generator_compare;
    const auto reference = profile.initial_reference_compare;
    if (profile.board_identity == 0U || generator == 0U ||
        generator > Period || reference > Period) {
        // Invalid input must not cause cleanup writes to a preceding carrier.
        if (binding.state != Stm32AnalogPowerState::Fault) {
            binding.fault = Stm32AnalogPowerFault::InvalidProfile;
        }
        binding.qualified = false;
        binding.state = Stm32AnalogPowerState::Fault;
        return false;
    }
    if (binding.may_be_active) { return fail(Stm32AnalogPowerFault::Busy); }
    binding.state = Stm32AnalogPowerState::Unqualified;
    binding.fault = Stm32AnalogPowerFault::None;
    binding.qualified = false;
    binding.timer_owned = false;
    if (!stm32_foreground()) { return fail(Stm32AnalogPowerFault::Execution); }
    if (!clocks_match()) { return fail(Stm32AnalogPowerFault::Clocks); }
    if (!pin_routing_available() || !stm32_stopped_acquisition()) { return fail(Stm32AnalogPowerFault::Ownership); }
    // If already clocked, reject a live/foreign owner before enabling anything.
    if ((RCC->APB1ENR & RCC_APB1ENR_TIM4EN) != 0U && !timer_idle()) {
        return fail(Stm32AnalogPowerFault::Ownership);
    }
    __HAL_RCC_GPIOB_CLK_ENABLE();
    if ((RCC->APB2ENR & RCC_APB2ENR_IOPBEN) == 0U ||
        (RCC->APB2RSTR & RCC_APB2RSTR_IOPBRST) != 0U) { return fail(Stm32AnalogPowerFault::Clocks); }
    // Inspect the pin modes BEFORE enabling a previously gated timer clock:
    // retained AF mode plus an unknown retained CEN could resume a converter.
    if (pins_locked() || !pins_disconnected_for_start()) { return fail(Stm32AnalogPowerFault::Ownership); }
    __HAL_RCC_TIM4_CLK_ENABLE();
    if (!peripheral_clocks()) { return fail(Stm32AnalogPowerFault::Clocks); }
    if (pins_locked() || !timer_idle()) { return fail(Stm32AnalogPowerFault::Ownership); }
    GPIOB->BSRR = Pins << 16U;
    __DSB();
    configure_gpio(GPIO_MODE_OUTPUT_PP);
    if (!gpio_low()) { return fail(Stm32AnalogPowerFault::Configuration); }

    binding.generator = generator;
    binding.reference = reference;
    binding.timer_owned = true;
    // This local handle is used only by synchronous setup/start functions. No
    // DMA/IRQ/callback registration retains it. Runtime ownership is register-
    // based; the weak PWM MSP hook must remain non-retaining and side-effect-free.
    TIM_HandleTypeDef timer{};
    timer.Instance = TIM4;
    timer.Init.Prescaler = 0U;
    timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    timer.Init.Period = Period;
    timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    timer.Init.RepetitionCounter = 0U;
    timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    TIM4->CR1 = 0U;
    TIM4->CR2 = 0U;
    TIM4->SMCR = 0U;
    TIM4->DIER = 0U;
    TIM4->CCMR1 = 0U;
    TIM4->CCMR2 = 0U;
    TIM4->CCR1 = 0U;
    TIM4->CCR2 = 0U;
    TIM4->DCR = 0U;
    if (HAL_TIM_PWM_Init(&timer) != HAL_OK) { return fail(Stm32AnalogPowerFault::Configuration); }
    TIM_OC_InitTypeDef channel{};
    channel.OCMode = TIM_OCMODE_PWM1;
    channel.Pulse = binding.reference;
    channel.OCPolarity = TIM_OCPOLARITY_HIGH;
    channel.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&timer, &channel, TIM_CHANNEL_3) != HAL_OK) {
        return fail(Stm32AnalogPowerFault::Configuration);
    }
    channel.Pulse = binding.generator;
    if (HAL_TIM_PWM_ConfigChannel(&timer, &channel, TIM_CHANNEL_4) != HAL_OK) {
        return fail(Stm32AnalogPowerFault::Configuration);
    }
    // Load both compare shadows and the prescaler while all physical power
    // pins remain GPIO-low. No update generation is permitted after start.
    TIM4->EGR = TIM_EGR_UG;
    TIM4->SR = 0U;
    TIM4->CNT = 0U;
    if (!timer_matches(false)) { return fail(Stm32AnalogPowerFault::Configuration); }
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    if (HAL_TIM_PWM_Start(&timer, TIM_CHANNEL_4) != HAL_OK ||
        HAL_TIM_PWM_Start(&timer, TIM_CHANNEL_3) != HAL_OK || !timer_matches(true)) {
        return fail(Stm32AnalogPowerFault::Configuration);
    }
    if (!progressing_counter()) { return fail(Stm32AnalogPowerFault::Timing); }
    // Set history BEFORE the GPIO call: even a partial mode write may connect
    // one output. Every subsequent failure preserves the existing carrier.
    binding.may_be_active = true;
    configure_gpio(GPIO_MODE_AF_PP);
    if (!gpio_alternate() || !timer_matches(true)) { return fail(Stm32AnalogPowerFault::Configuration); }
    binding.qualified = true;
    binding.fault = Stm32AnalogPowerFault::None;
    binding.state = Stm32AnalogPowerState::CarrierRunning;
    return true;
}

bool ready() noexcept
{
    if (!binding.qualified || binding.state != Stm32AnalogPowerState::CarrierRunning) { return false; }
    if (!stm32_foreground()) { return fail(Stm32AnalogPowerFault::Execution); }
    if (!clocks_match() || !peripheral_clocks()) { return fail(Stm32AnalogPowerFault::Clocks); }
    if (!pin_routing_available() || pins_locked()) { return fail(Stm32AnalogPowerFault::Ownership); }
    if (!gpio_alternate() || !timer_matches(true)) { return fail(Stm32AnalogPowerFault::Configuration); }
    return true;
}

bool queue_reference(std::uint16_t compare) noexcept
{
    if (compare > Period) { return false; }
    if (!ready()) { return false; }
    TIM4->CCR3 = compare;
    if (TIM4->CCR3 != compare) { return fail(Stm32AnalogPowerFault::Configuration); }
    binding.reference = compare;
    return true;
}

bool shutdown(Stm32AnalogPowerQualification q) noexcept
{
    if (!qualified(q)) { return revoke(); }
    if (!binding.timer_owned) { return false; }
    // Qualified shutdown is allowed after RAM revocation/fault, but never
    // after the register/pin owner was replaced. No blind force-stop exists.
    if (!stm32_foreground()) { return fail(Stm32AnalogPowerFault::Execution); }
    if (!clocks_match() || !peripheral_clocks()) { return fail(Stm32AnalogPowerFault::Clocks); }
    if (!pin_routing_available() || pins_locked()) { return fail(Stm32AnalogPowerFault::Ownership); }
    if (!pins_owned_for_shutdown() || !timer_owned_for_shutdown() || !stm32_stopped_acquisition()) {
        return fail(Stm32AnalogPowerFault::Ownership);
    }
    // Remove the physical alternate function while the timer is still running.
    // Its low ODR latch was never changed. Only after readback may CEN stop.
    configure_gpio(GPIO_MODE_OUTPUT_PP);
    if (!gpio_low()) { return fail(Stm32AnalogPowerFault::Configuration); }
    CLEAR_BIT(TIM4->CCER, Enables);
    CLEAR_BIT(TIM4->CR1, TIM_CR1_CEN);
    __DSB();
    if ((TIM4->CCER & Enables) != 0U || (TIM4->CR1 & TIM_CR1_CEN) != 0U) {
        return fail(Stm32AnalogPowerFault::Configuration);
    }
    binding = Binding{};
    return true;
}

Stm32AnalogPowerState state() noexcept { return binding.state; }
Stm32AnalogPowerFault fault() noexcept { return binding.fault; }
bool carrier_may_be_active() noexcept { return binding.may_be_active; }

const Stm32AnalogPowerApi Api{start, queue_reference, shutdown, state, fault, carrier_may_be_active};

} // namespace

const Stm32AnalogPowerApi& stm32_analog_power_api() noexcept { return Api; }

} // namespace dso
