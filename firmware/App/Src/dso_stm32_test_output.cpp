#include "dso_stm32_test_output.hpp"
#include "dso_stm32_shared_guard.hpp"
#include "main.h"

namespace dso {
namespace {

static_assert(USE_HAL_TIM_REGISTER_CALLBACKS == 0U,
    "Local TIM handle requires disabled callbacks and a non-retaining PWM MSP hook");

constexpr std::uint32_t Pin{1U << 7U};
constexpr std::uint32_t ModeMask{15U << 28U};
constexpr std::uint32_t GpioLowMode{2U << 28U};
constexpr std::uint32_t AlternateMode{10U << 28U};
constexpr std::uint32_t Period{35999U};
constexpr std::uint32_t Compare{18000U};
constexpr std::uint32_t PwmMode{(TIM_OCMODE_PWM1 << 8U) | TIM_CCMR1_OC2PE};
constexpr std::uint32_t ProgressDeadlineCycles{216000U}; // Three nominal periods.
// Even an unrealistically fast one-cycle iteration cannot exhaust this cap
// before the cycle deadline. The separate cap also bounds a stalled DWT.
constexpr unsigned ProgressIterations{216001U};

struct Binding final {
    Stm32TestOutputState state{Stm32TestOutputState::Unqualified};
    Stm32TestOutputFault fault{Stm32TestOutputFault::None};
    bool timer_owned{};
    bool may_be_active{};
};
Binding binding;
static_assert(sizeof(Binding) <= 4U, "Keep the dormant adapter state small");

bool qualified(Stm32TestOutputQualification q) noexcept
{
    return q.pinmap_verified && q.output_load_verified && q.waveform_profile_verified &&
        q.start_stop_sequence_verified;
}

bool routing_available() noexcept;
bool trigger_consumers_available() noexcept;
bool pins_available() noexcept;

bool startup_contract_complete(Stm32Tim3StartupContract c) noexcept
{
    return c.device_wide_system_reset && c.direct_main_flash_boot &&
        c.no_bootloader_or_earlier_tim3_owner && c.no_prior_tim3_configuration;
}

bool reset_image_matches() noexcept
{
    return TIM3->CR1 == 0U && TIM3->CR2 == 0U && TIM3->SMCR == 0U && TIM3->DIER == 0U &&
        TIM3->SR == 0U && TIM3->CCMR1 == 0U && TIM3->CCMR2 == 0U &&
        TIM3->CCER == 0U && TIM3->CNT == 0U && TIM3->PSC == 0U && TIM3->ARR == 0xFFFFU &&
        TIM3->CCR1 == 0U && TIM3->CCR2 == 0U && TIM3->CCR3 == 0U && TIM3->CCR4 == 0U &&
        TIM3->DCR == 0U;
}

bool clock_tree_matches() noexcept
{
    if (HAL_RCC_GetSysClockFreq() != 72000000U || HAL_RCC_GetHCLKFreq() != 72000000U) { return false; }
    RCC_ClkInitTypeDef clocks{};
    std::uint32_t latency{};
    HAL_RCC_GetClockConfig(&clocks, &latency);
    const auto pclk1 = HAL_RCC_GetPCLK1Freq();
    return pclk1 <= 36000000U && pclk1 *
        (clocks.APB1CLKDivider == RCC_HCLK_DIV1 ? 1U : 2U) == 72000000U;
}

bool provision_after_reset(Stm32Tim3StartupContract c, Stm32TestOutputQualification q) noexcept
{
    // An incomplete/unknown boot contract has no peripheral side effects.
    if (!qualified(q) || !startup_contract_complete(c)) { return false; }
    if (binding.timer_owned || binding.may_be_active || !stm32_foreground()) { return false; }
    constexpr auto ports = RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN | RCC_APB2ENR_AFIOEN;
    constexpr auto resets = RCC_APB2RSTR_IOPARST | RCC_APB2RSTR_IOPBRST | RCC_APB2RSTR_AFIORST;
    if ((RCC->APB2ENR & ports) != ports || (RCC->APB2RSTR & resets) != 0U ||
        (RCC->APB1RSTR & RCC_APB1RSTR_TIM3RST) != 0U || !clock_tree_matches()) { return false; }
    // Check all known cross-peripheral consumers, routing and pins before
    // opening TIM3's gate. These functions read no TIM3 register.
    if (!routing_available() || !trigger_consumers_available() || !pins_available()) { return false; }
    const auto before = static_cast<std::uint32_t>(RCC->APB1ENR);
    if ((before & RCC_APB1ENR_TIM3EN) != 0U) { return false; }
    RCC->APB1ENR = before | RCC_APB1ENR_TIM3EN;
    __DSB();
    if ((RCC->APB1ENR & RCC_APB1ENR_TIM3EN) == 0U ||
        (RCC->APB1ENR & ~RCC_APB1ENR_TIM3EN) != (before & ~RCC_APB1ENR_TIM3EN) ||
        (RCC->APB1RSTR & RCC_APB1RSTR_TIM3RST) != 0U || !reset_image_matches()) {
        binding.fault = Stm32TestOutputFault::Ownership;
        binding.state = Stm32TestOutputState::Fault;
        return false;
    }
    return true;
}

bool revoke() noexcept
{
    binding.state = Stm32TestOutputState::Unqualified;
    binding.fault = Stm32TestOutputFault::Qualification;
    return false;
}

bool clocks_match() noexcept
{
    // Never inspect a peripheral behind a disabled interface clock or reset.
    constexpr auto ports = RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN | RCC_APB2ENR_AFIOEN;
    constexpr auto resets = RCC_APB2RSTR_IOPARST | RCC_APB2RSTR_IOPBRST | RCC_APB2RSTR_AFIORST;
    if ((RCC->APB2ENR & ports) != ports || (RCC->APB1ENR & RCC_APB1ENR_TIM3EN) == 0U ||
        (RCC->APB2RSTR & resets) != 0U || (RCC->APB1RSTR & RCC_APB1RSTR_TIM3RST) != 0U) {
        return false;
    }
    return clock_tree_matches();
}

bool routing_available() noexcept
{
    const auto map = static_cast<std::uint32_t>(AFIO->MAPR);
    // TIM1 partial remap can place its complementary output on PA7. SPI1 MOSI
    // also uses PA7 when not remapped. Inspect only defined remap fields, never
    // SWJ_CFG readback; neither MAPR nor any competing peripheral is written.
    if ((map & (AFIO_MAPR_TIM3_REMAP | AFIO_MAPR_TIM1_REMAP)) != 0U ||
        (DBGMCU->CR & DBGMCU_CR_DBG_TIM3_STOP) != 0U) { return false; }
    if ((map & AFIO_MAPR_SPI1_REMAP) == 0U && (RCC->APB2ENR & RCC_APB2ENR_SPI1EN) != 0U &&
        (SPI1->CR1 & SPI_CR1_SPE) != 0U) { return false; }
    const auto lock = static_cast<std::uint32_t>(GPIOA->LCKR);
    return (lock & GPIO_LCKR_LCKK) == 0U || (lock & Pin) == 0U;
}

bool trigger_consumers_available() noexcept
{
    // MMS=reset still exposes software UG as TIM3_TRGO. Clocked TIM1/2/4
    // receive it on ITR2. A disabled physical CH4 pin also does not suppress
    // its internal compare event, which an ADC injected trigger may select.
    const auto apb1 = static_cast<std::uint32_t>(RCC->APB1ENR);
    const auto apb2 = static_cast<std::uint32_t>(RCC->APB2ENR);
    const auto slave_conflict = [](std::uint32_t smcr) noexcept {
        return (smcr & TIM_SMCR_SMS) != 0U && (smcr & TIM_SMCR_TS) == TIM_TS_ITR2;
    };
    if (((apb2 & RCC_APB2ENR_TIM1EN) != 0U && slave_conflict(TIM1->SMCR)) ||
        ((apb1 & RCC_APB1ENR_TIM2EN) != 0U && slave_conflict(TIM2->SMCR)) ||
        ((apb1 & RCC_APB1ENR_TIM4EN) != 0U && slave_conflict(TIM4->SMCR))) { return false; }
    const auto adc_conflict = [](std::uint32_t cr2) noexcept {
        return ((cr2 & ADC_CR2_EXTTRIG) != 0U && (cr2 & ADC_CR2_EXTSEL) == (4U << 17U)) ||
            ((cr2 & ADC_CR2_JEXTTRIG) != 0U && (cr2 & ADC_CR2_JEXTSEL) == (4U << 12U));
    };
    return ((apb2 & RCC_APB2ENR_ADC1EN) == 0U || !adc_conflict(ADC1->CR2)) &&
        ((apb2 & RCC_APB2ENR_ADC2EN) == 0U || !adc_conflict(ADC2->CR2));
}

bool alternate_output(std::uint32_t mode) noexcept
{
    return (mode & 3U) != 0U && (mode & 8U) != 0U;
}

bool pins_available() noexcept
{
    const auto a = static_cast<std::uint32_t>(GPIOA->CRL);
    const auto b = static_cast<std::uint32_t>(GPIOB->CRL);
    if (alternate_output((a >> 24U) & 15U) || alternate_output(b & 15U) ||
        alternate_output((b >> 4U) & 15U)) { return false; }
    const auto mode = a >> 28U;
    return mode == 0U || mode == 4U || mode == 8U ||
        (mode == 2U && (GPIOA->ODR & Pin) == 0U);
}

bool gpio_low() noexcept
{
    return (GPIOA->CRL & ModeMask) == GpioLowMode && (GPIOA->ODR & Pin) == 0U;
}

bool pins_owned() noexcept
{
    const auto mode = static_cast<std::uint32_t>(GPIOA->CRL) & ModeMask;
    return (mode == GpioLowMode || mode == AlternateMode) && (GPIOA->ODR & Pin) == 0U;
}

bool idle() noexcept
{
    return (TIM3->CR1 & TIM_CR1_CEN) == 0U && TIM3->CR2 == 0U &&
        TIM3->CCER == 0U && TIM3->DIER == 0U && TIM3->SMCR == 0U;
}

bool timer_matches(bool running, bool recovering = false) noexcept
{
    const auto cr1 = static_cast<std::uint32_t>(TIM3->CR1);
    const auto ccer = static_cast<std::uint32_t>(TIM3->CCER);
    const bool enables = recovering ? (cr1 & ~TIM_CR1_CEN) == TIM_CR1_ARPE &&
        (ccer & ~TIM_CCER_CC2E) == 0U :
        cr1 == (TIM_CR1_ARPE | (running ? TIM_CR1_CEN : 0U)) &&
        ccer == (running ? TIM_CCER_CC2E : 0U);
    return enables && TIM3->CR2 == 0U && TIM3->SMCR == 0U && TIM3->DIER == 0U &&
        TIM3->CCMR1 == PwmMode && TIM3->CCMR2 == 0U && TIM3->PSC == 1U && TIM3->ARR == Period &&
        TIM3->CCR1 == 0U && TIM3->CCR2 == Compare && TIM3->CCR3 == 0U && TIM3->CCR4 == 0U && TIM3->DCR == 0U;
}

void configure_gpio(std::uint32_t mode) noexcept
{
    GPIO_InitTypeDef pin{};
    pin.Pin = Pin;
    pin.Mode = mode;
    pin.Pull = GPIO_NOPULL;
    pin.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &pin);
}

void cleanup_disconnected() noexcept
{
    // Provenance and a confirmed disconnected output are both required. If a
    // failed setup left other channel/request enables, do not stop that state
    // blindly. No cleanup is attempted once AF handoff may have occurred.
    if (!binding.timer_owned || binding.may_be_active || !stm32_foreground() || !clocks_match() || !gpio_low() ||
        (TIM3->CCER & ~TIM_CCER_CC2E) != 0U || TIM3->DIER != 0U || TIM3->SMCR != 0U) { return; }
    CLEAR_BIT(TIM3->CCER, TIM_CCER_CC2E);
    CLEAR_BIT(TIM3->CR1, TIM_CR1_CEN);
    __DSB();
    if ((TIM3->CCER & TIM_CCER_CC2E) == 0U && (TIM3->CR1 & TIM_CR1_CEN) == 0U) {
        binding.timer_owned = false;
    }
}

bool fail(Stm32TestOutputFault reason) noexcept
{
    if (binding.state != Stm32TestOutputState::Fault) {
        binding.fault = reason;
        cleanup_disconnected();
        binding.state = Stm32TestOutputState::Fault;
    }
    return false;
}

bool observe_update() noexcept
{
    if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0U ||
        (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U) { return false; }
    const auto beginning = static_cast<std::uint32_t>(DWT->CYCCNT);
    auto previous = static_cast<std::uint32_t>(TIM3->CNT);
    std::uint32_t last_elapsed{};
    if (previous > 65535U) { return false; }
    for (unsigned i{}; i < ProgressIterations; ++i) {
        const auto current = static_cast<std::uint32_t>(TIM3->CNT);
        const auto elapsed = static_cast<std::uint32_t>(DWT->CYCCNT - beginning);
        if (current > 65535U || elapsed <= last_elapsed || elapsed >= ProgressDeadlineCycles) { return false; }
        // A natural overflow proves that PSC/ARR/CCR shadows were transferred,
        // including if setup's software UG was lost. Until that first update,
        // a retained active ARR can legally exceed the new preload value.
        if (elapsed >= 72U && current < previous && current <= Period) { return true; }
        previous = current;
        last_elapsed = elapsed;
        __NOP();
    }
    return false;
}

bool start(const Stm32TestOutputProfile& profile, Stm32TestOutputQualification q) noexcept
{
    if (!qualified(q)) { return revoke(); }
    if (profile.board_identity == 0U) {
        // Invalid input must never trigger cleanup of an earlier output.
        if (binding.state != Stm32TestOutputState::Fault) { binding.fault = Stm32TestOutputFault::InvalidProfile; }
        binding.state = Stm32TestOutputState::Fault;
        return false;
    }
    if (binding.timer_owned || binding.may_be_active) { return fail(Stm32TestOutputFault::Busy); }
    binding.state = Stm32TestOutputState::Unqualified;
    binding.fault = Stm32TestOutputFault::None;
    if (!stm32_foreground()) { return fail(Stm32TestOutputFault::Execution); }
    if (!clocks_match()) { return fail(Stm32TestOutputFault::Clocks); }
    if (!routing_available() || !trigger_consumers_available() || !pins_available() || !idle()) {
        return fail(Stm32TestOutputFault::Ownership);
    }
    GPIOA->BSRR = Pin << 16U;
    __DSB();
    if ((GPIOA->ODR & Pin) != 0U) { return fail(Stm32TestOutputFault::Configuration); }
    configure_gpio(GPIO_MODE_OUTPUT_PP);
    if (!gpio_low()) { return fail(Stm32TestOutputFault::Configuration); }
    binding.timer_owned = true;
    TIM_HandleTypeDef timer{};
    timer.Instance = TIM3;
    timer.Init.Prescaler = 1U;
    timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    timer.Init.Period = Period;
    timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    TIM3->CR1 = 0U;
    TIM3->CR2 = 0U;
    TIM3->SMCR = 0U;
    TIM3->DIER = 0U;
    TIM3->CCMR1 = 0U;
    TIM3->CCMR2 = 0U;
    TIM3->CCR1 = 0U;
    TIM3->CCR3 = 0U;
    TIM3->CCR4 = 0U;
    TIM3->DCR = 0U;
    if (HAL_TIM_PWM_Init(&timer) != HAL_OK) { return fail(Stm32TestOutputFault::Configuration); }
    TIM_OC_InitTypeDef channel{};
    channel.OCMode = TIM_OCMODE_PWM1;
    channel.Pulse = Compare;
    channel.OCPolarity = TIM_OCPOLARITY_HIGH;
    channel.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&timer, &channel, TIM_CHANNEL_2) != HAL_OK) {
        return fail(Stm32TestOutputFault::Configuration);
    }
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0U;
    TIM3->CNT = 0U;
    if (!timer_matches(false)) { return fail(Stm32TestOutputFault::Configuration); }
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    if (HAL_TIM_PWM_Start(&timer, TIM_CHANNEL_2) != HAL_OK || !timer_matches(true)) {
        return fail(Stm32TestOutputFault::Configuration);
    }
    if (!observe_update()) { return fail(Stm32TestOutputFault::Timing); }
    // Handoff can partially succeed; retain history before touching its mode.
    binding.may_be_active = true;
    configure_gpio(GPIO_MODE_AF_PP);
    if ((GPIOA->CRL & ModeMask) != AlternateMode || (GPIOA->ODR & Pin) != 0U || !timer_matches(true)) {
        return fail(Stm32TestOutputFault::Configuration);
    }
    binding.state = Stm32TestOutputState::Running;
    binding.fault = Stm32TestOutputFault::None;
    return true;
}

bool shutdown(Stm32TestOutputQualification q) noexcept
{
    if (!qualified(q)) { return revoke(); }
    if (!binding.timer_owned) { return false; }
    if (!stm32_foreground()) { return fail(Stm32TestOutputFault::Execution); }
    if (!clocks_match()) { return fail(Stm32TestOutputFault::Clocks); }
    if (!routing_available() || !trigger_consumers_available() || !pins_owned() || !timer_matches(false, true)) {
        return fail(Stm32TestOutputFault::Ownership);
    }
    // Disconnect the pin while the internal PWM continues. Stop only after
    // confirming its GPIO-low mode/latch; retry may recover a partial stop.
    configure_gpio(GPIO_MODE_OUTPUT_PP);
    if (!gpio_low()) { return fail(Stm32TestOutputFault::Configuration); }
    CLEAR_BIT(TIM3->CCER, TIM_CCER_CC2E);
    CLEAR_BIT(TIM3->CR1, TIM_CR1_CEN);
    __DSB();
    if ((TIM3->CCER & TIM_CCER_CC2E) != 0U || (TIM3->CR1 & TIM_CR1_CEN) != 0U) {
        return fail(Stm32TestOutputFault::Configuration);
    }
    binding = Binding{};
    return true;
}

Stm32TestOutputState state() noexcept { return binding.state; }
Stm32TestOutputFault fault() noexcept { return binding.fault; }
bool output_may_be_active() noexcept { return binding.may_be_active; }
const Stm32TestOutputApi Api{start, shutdown, state, fault, output_may_be_active};

} // namespace

bool start_stm32_test_output(const Stm32TestOutputProfile& profile,
    Stm32TestOutputQualification q) noexcept
{
    return start(profile, q);
}

bool start_stm32_test_output_after_system_reset(const Stm32TestOutputProfile& profile,
    Stm32Tim3StartupContract contract, Stm32TestOutputQualification q) noexcept
{
    // Preserve standard-start validation order without touching peripherals.
    if (!qualified(q)) { return revoke(); }
    if (profile.board_identity == 0U) {
        if (binding.state != Stm32TestOutputState::Fault) { binding.fault = Stm32TestOutputFault::InvalidProfile; }
        binding.state = Stm32TestOutputState::Fault;
        return false;
    }
    if (!provision_after_reset(contract, q)) { return false; }
    return start(profile, q);
}

const Stm32TestOutputApi& stm32_test_output_api() noexcept { return Api; }

} // namespace dso
