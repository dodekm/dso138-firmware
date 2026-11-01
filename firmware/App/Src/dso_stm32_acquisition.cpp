#include "dso_stm32_acquisition.hpp"
#include "dso_stm32_adc.hpp"
#include "dso_rolling.hpp"
#include "main.h"

extern "C" {
extern ADC_HandleTypeDef hadc1;
extern DMA_HandleTypeDef hdma_adc1;
extern TIM_HandleTypeDef htim2;
}

namespace dso {
namespace {

enum class Backend : std::uint8_t { None, Fast, Rolling, FilteredRolling };
constexpr std::uint16_t RollingDmaCount{2U};
constexpr std::uint16_t OversampledRollingDmaCount{16U};
static_assert(OversampledRollingDmaCount == RollingOversampledDmaSamples,
              "Filtered STM32 transport must match the portable DMA allocation");
constexpr std::uint32_t MinimumRollingDivider{14062500U}; // 100 s / 512 points at 72 MHz.
constexpr std::uint32_t MaximumRollingDivider{703125000U}; // 5000 s / 512 points.

bool oversampled_divider_valid(std::uint32_t divider) noexcept
{
    // Exact 25:1 ratios of the six 512-point output plans, not a free-form
    // frequency range. Keep the factory's hardware contract independently
    // checked rather than relying on portable caller validation alone.
    return divider == 562500U || divider == 1125000U || divider == 2812500U ||
        divider == 5625000U || divider == 11250000U || divider == 28125000U;
}

struct Context final {
    Stm32AcquisitionQualification qualification{};
    volatile std::uint32_t error_latch{};
    bool prepared{};
    bool dma_armed{};
    Backend backend{Backend::None};
    std::uint16_t expected_count{};
    std::uint16_t prescaler{}, period{};
};
Context context;

bool stopped_timer_ready() noexcept
{
    return (RCC->APB1ENR & RCC_APB1ENR_TIM2EN) != 0U &&
        TIM2->CR1 == 0U && TIM2->CR2 == TIM_TRGO_OC2REF &&
        TIM2->SMCR == 0U && TIM2->DIER == 0U && TIM2->CCER == 0U &&
        TIM2->CCMR1 == (TIM_CCMR1_OC2M | TIM_CCMR1_OC2PE) && TIM2->CCMR2 == 0U &&
        TIM2->PSC == context.prescaler && TIM2->ARR == context.period && TIM2->CNT == 0U &&
        TIM2->CCR2 == (static_cast<std::uint32_t>(context.period) + 1U) / 2U;
}

void stop_trigger(void*) noexcept
{
    // Stop the clock before disconnecting the internal ADC channel signal.
    // PA1 remains analog; CC2E is required by the on-chip ADC trigger route.
    CLEAR_BIT(TIM2->CR1, TIM_CR1_CEN);
    CLEAR_BIT(TIM2->CCER, TIM_CCER_CC2E);
    __DMB();
    if ((TIM2->CR1 & TIM_CR1_CEN) != 0U || (TIM2->CCER & TIM_CCER_CC2E) != 0U) { context.error_latch = 4U; }
}

bool stop_dma(void*) noexcept
{
    // Bounded foreground abort: HAL_ADC_Stop_DMA contains tick-dependent wait
    // loops. No other code owns these handles/channel. Discard in-flight data,
    // disable requests/channel, verify stopped, then reconcile HAL bookkeeping.
    CLEAR_BIT(ADC1->CR2, ADC_CR2_DMA | ADC_CR2_EXTTRIG | ADC_CR2_ADON);
    CLEAR_BIT(DMA1_Channel1->CCR, DMA_CCR_EN | DMA_CCR_HTIE | DMA_CCR_TCIE | DMA_CCR_TEIE);
    __DMB();
    const bool stopped = (DMA1_Channel1->CCR & (DMA_CCR_EN | DMA_CCR_HTIE | DMA_CCR_TCIE | DMA_CCR_TEIE)) == 0U &&
        (ADC1->CR2 & (ADC_CR2_DMA | ADC_CR2_EXTTRIG | ADC_CR2_ADON)) == 0U;
    if (stopped) {
        if (context.error_latch == 0U && (DMA1->ISR & DMA_ISR_TEIF1) != 0U) { context.error_latch = 1U; }
        DMA1->IFCR = DMA_IFCR_CHTIF1 | DMA_IFCR_CTCIF1 | DMA_IFCR_CTEIF1;
        hadc1.State = HAL_ADC_STATE_READY;
        hadc1.ErrorCode = HAL_ADC_ERROR_NONE;
        hadc1.Lock = HAL_UNLOCKED;
        hdma_adc1.State = HAL_DMA_STATE_READY;
        hdma_adc1.ErrorCode = HAL_DMA_ERROR_NONE;
        hdma_adc1.Lock = HAL_UNLOCKED;
    } else { context.error_latch = 4U; }
    context.dma_armed = false;
    context.prepared = false;
    context.backend = Backend::None;
    context.expected_count = 0U;
    __DMB();
    return stopped && context.error_latch == 0U;
}

bool stop_acquisition(void* value) noexcept
{
    stop_trigger(value);
    return stop_dma(value);
}

bool prepare_backend(const TimebasePlan& plan, Backend backend) noexcept
{
    context.prepared = false;
    context.dma_armed = false;
    context.backend = Backend::None;
    context.expected_count = 0U;
    if (!context.qualification.analog_power_verified ||
        !context.qualification.adc_input_settling_verified ||
        __get_IPSR() != 0U || __get_PRIMASK() != 0U || __get_BASEPRI() != 0U || __get_FAULTMASK() != 0U ||
        hadc1.Instance != ADC1 || hdma_adc1.Instance != DMA1_Channel1 ||
        htim2.Instance != TIM2 || hadc1.DMA_Handle != &hdma_adc1 ||
        HAL_RCC_GetSysClockFreq() != 72000000U || plan.timer_hz != 72000000U ||
        HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_ADC) != plan.adc_hz ||
        plan.adc_hz != 12000000U || plan.trigger_cycles_x2 != 46U ||
        (RCC->APB1ENR & RCC_APB1ENR_TIM2EN) == 0U ||
        (RCC->APB2ENR & (RCC_APB2ENR_ADC1EN | RCC_APB2ENR_ADC2EN | RCC_APB2ENR_IOPAEN)) !=
            (RCC_APB2ENR_ADC1EN | RCC_APB2ENR_ADC2EN | RCC_APB2ENR_IOPAEN) ||
        (RCC->AHBENR & RCC_AHBENR_DMA1EN) == 0U ||
        (GPIOA->CRL & 0xffffU) != 0U ||
        ((RCC->APB2ENR & RCC_APB2ENR_TIM1EN) != 0U && (TIM1->CR1 & TIM_CR1_CEN) != 0U) ||
        (ADC1->CR1 & ADC_CR1_DUALMOD) != 0U ||
        (ADC2->CR2 & ADC_CR2_ADON) != 0U ||
        (DMA1_Channel1->CCR & DMA_CCR_EN) != 0U ||
        TIM2->CR1 != 0U || TIM2->SMCR != 0U || TIM2->DIER != 0U || TIM2->CCER != 0U ||
        TIM2->CR2 != TIM_TRGO_OC2REF || TIM2->CCMR2 != 0U ||
        (TIM2->CCMR1 != 0U && TIM2->CCMR1 != (TIM_CCMR1_OC2M | TIM_CCMR1_OC2PE)) ||
        htim2.State != HAL_TIM_STATE_READY || htim2.Lock != HAL_UNLOCKED ||
        htim2.Init.CounterMode != TIM_COUNTERMODE_UP || htim2.Init.ClockDivision != TIM_CLOCKDIVISION_DIV1 ||
        htim2.Init.AutoReloadPreload != TIM_AUTORELOAD_PRELOAD_DISABLE ||
        hadc1.Init.ScanConvMode != ADC_SCAN_DISABLE ||
        hadc1.Init.ContinuousConvMode != DISABLE ||
        hadc1.Init.DiscontinuousConvMode != DISABLE ||
        hadc1.Init.DataAlign != ADC_DATAALIGN_RIGHT ||
        hadc1.Init.NbrOfConversion != 1U ||
        hadc1.Init.ExternalTrigConv != ADC_EXTERNALTRIGCONV_T2_CC2 || plan.period == 0U || plan.divider < 138U ||
        (std::uint64_t{plan.prescaler} + 1U) * (std::uint64_t{plan.period} + 1U) != plan.divider) { return false; }
    if (backend == Backend::Rolling &&
        (plan.divider < MinimumRollingDivider || plan.divider > MaximumRollingDivider)) {
        return false;
    }
    if (backend == Backend::FilteredRolling &&
        !oversampled_divider_valid(plan.divider)) {
        return false;
    }
    RCC_ClkInitTypeDef clocks{};
    std::uint32_t latency{};
    HAL_RCC_GetClockConfig(&clocks, &latency);
    const auto pclk1_hz = HAL_RCC_GetPCLK1Freq();
    const auto timer_clock = pclk1_hz *
        (clocks.APB1CLKDivider == RCC_HCLK_DIV1 ? 1U : 2U);
    // DS5319 section2.3.7: APB1 is limited to36MHz even when TIM2 runs at72MHz.
    // A /1 APB clock at72MHz must not pass merely because TIM2 also reads72MHz.
    if (pclk1_hz > 36000000U || timer_clock != plan.timer_hz) { return false; }
    CLEAR_BIT(ADC1->CR2, ADC_CR2_EXTTRIG | ADC_CR2_ADON);
    // Destination clock already enabled by generated ADC MSP. Preload/update
    // occurs while the ADC trigger is DISARMED, eliminating hidden sample zero.
    // MMS=Reset would export UG. Enable mode exports CEN=0 throughout setup;
    // the future comparator slave must also remain disarmed through this phase.
    MODIFY_REG(TIM2->CR2, TIM_CR2_MMS, TIM_TRGO_ENABLE);
    __HAL_TIM_SET_PRESCALER(&htim2, plan.prescaler);
    __HAL_TIM_SET_AUTORELOAD(&htim2, plan.period);
    TIM_OC_InitTypeDef compare{};
    compare.OCMode = TIM_OCMODE_PWM2;
    compare.Pulse = (static_cast<std::uint32_t>(plan.period) + 1U) / 2U;
    compare.OCPolarity = TIM_OCPOLARITY_HIGH;
    compare.OCFastMode = TIM_OCFAST_DISABLE;
    // HAL enables OC2 preload; it restores the prior CCER, required zero above.
    // No PWM_Start/MSP post-init or GPIO alternate-function output is used.
    if (HAL_TIM_PWM_ConfigChannel(&htim2, &compare, TIM_CHANNEL_2) != HAL_OK) { return false; }
    __HAL_TIM_SET_COUNTER(&htim2, 0U);
    if (HAL_TIM_GenerateEvent(&htim2, TIM_EVENTSOURCE_UPDATE) != HAL_OK) { return false; }
    __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE | TIM_FLAG_CC2);
    MODIFY_REG(TIM2->CR2, TIM_CR2_MMS, TIM_TRGO_OC2REF);
    if (TIM2->CR1 != 0U || TIM2->CCER != 0U || TIM2->CCMR1 != (TIM_CCMR1_OC2M | TIM_CCMR1_OC2PE) ||
        TIM2->CCR2 != compare.Pulse || TIM2->PSC != plan.prescaler || TIM2->ARR != plan.period ||
        TIM2->CNT != 0U || TIM2->CR2 != TIM_TRGO_OC2REF) { return false; }
    if (!calibrate_stm32_adc(hadc1)) { return false; }
    ADC_ChannelConfTypeDef channel{};
    channel.Channel = ADC_CHANNEL_0;
    channel.Rank = ADC_REGULAR_RANK_1;
    channel.SamplingTime = ADC_SAMPLETIME_7CYCLES_5;
    if (HAL_ADC_ConfigChannel(&hadc1, &channel) != HAL_OK) { return false; }
    // Freezing TIM2 limits debug-side damage, but a halt still INVALIDATES the
    // real-time epoch: never claim continuity through a debugging session.
    __HAL_DBGMCU_FREEZE_TIM2();
    context.error_latch = 0U;
    HAL_NVIC_ClearPendingIRQ(DMA1_Channel1_IRQn);
    context.backend = backend;
    context.prescaler = plan.prescaler; context.period = plan.period;
    context.expected_count = backend == Backend::Rolling ? RollingDmaCount :
        (backend == Backend::FilteredRolling ? OversampledRollingDmaCount : DmaRingSamples);
    context.prepared = true;
    return true;
}

bool prepare(void*, const TimebasePlan& plan) noexcept
{ return prepare_backend(plan, Backend::Fast); }

bool prepare_rolling(void*, const TimebasePlan& plan) noexcept
{ return prepare_backend(plan, Backend::Rolling); }

bool prepare_oversampled_rolling(void*, const TimebasePlan& plan) noexcept
{ return prepare_backend(plan, Backend::FilteredRolling); }

bool arm_dma_backend(std::uint16_t* destination, std::uint16_t count, Backend backend) noexcept
{
    if (!context.prepared || context.backend != backend || destination == nullptr ||
        context.expected_count == 0U || count != context.expected_count ||
        (reinterpret_cast<std::uintptr_t>(destination) & 3U) != 0U ||
        hdma_adc1.State != HAL_DMA_STATE_READY || hdma_adc1.Lock != HAL_UNLOCKED ||
        (DMA1_Channel1->CCR & DMA_CCR_EN) != 0U || !stopped_timer_ready()) { return false; }
    // Pinned F1 HAL fails to propagate HAL_DMA_Start_IT errors. Verify actual
    // register state before the trigger source can generate any samples.
    if (!enable_stm32_adc_bounded(hadc1)) { return false; }
    if (HAL_ADC_Start_DMA(&hadc1, reinterpret_cast<std::uint32_t*>(destination), count) != HAL_OK) {
        return false;
    }
    __HAL_DMA_DISABLE_IT(&hdma_adc1, DMA_IT_HT | DMA_IT_TC);
    __HAL_DMA_ENABLE_IT(&hdma_adc1, DMA_IT_TE);
    const auto mask = DMA_CCR_EN | DMA_CCR_DIR | DMA_CCR_CIRC | DMA_CCR_PINC |
        DMA_CCR_MINC | DMA_CCR_PSIZE | DMA_CCR_MSIZE | DMA_CCR_MEM2MEM | DMA_CCR_PL |
        DMA_CCR_HTIE | DMA_CCR_TCIE | DMA_CCR_TEIE;
    const auto expected = DMA_CCR_EN | DMA_CCR_CIRC | DMA_CCR_MINC |
        DMA_CCR_PSIZE_0 | DMA_CCR_MSIZE_0 | DMA_CCR_PL | DMA_CCR_TEIE;
    const auto source = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&ADC1->DR));
    const auto memory = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(destination));
    const auto adc1_mask = ADC_CR1_SCAN | ADC_CR1_DISCEN | ADC_CR1_JDISCEN | ADC_CR1_JAUTO |
        ADC_CR1_DUALMOD | ADC_CR1_EOCIE | ADC_CR1_JEOCIE | ADC_CR1_AWDIE;
    const auto adc2_mask = ADC_CR2_CONT | ADC_CR2_ALIGN | ADC_CR2_EXTSEL |
        ADC_CR2_EXTTRIG | ADC_CR2_DMA | ADC_CR2_ADON | ADC_CR2_JEXTTRIG;
    const auto adc_expected = ADC_EXTERNALTRIGCONV_T2_CC2 | ADC_CR2_EXTTRIG | ADC_CR2_DMA | ADC_CR2_ADON;
    context.dma_armed = (DMA1_Channel1->CCR & mask) == expected &&
        DMA1_Channel1->CNDTR == count && DMA1_Channel1->CPAR == source &&
        DMA1_Channel1->CMAR == memory && hdma_adc1.State == HAL_DMA_STATE_BUSY &&
        (DMA1->ISR & (DMA_ISR_HTIF1 | DMA_ISR_TCIF1 | DMA_ISR_TEIF1)) == 0U &&
        (ADC1->CR1 & adc1_mask) == 0U && (ADC1->CR2 & adc2_mask) == adc_expected &&
        (ADC1->SQR1 & ADC_SQR1_L) == 0U && (ADC1->SQR3 & ADC_SQR3_SQ1) == 0U &&
        (ADC1->SMPR2 & ADC_SMPR2_SMP0) == ADC_SAMPLETIME_7CYCLES_5;
    return context.dma_armed;
}

bool arm_dma(void*, std::uint16_t* destination, std::uint16_t count) noexcept
{ return arm_dma_backend(destination, count, Backend::Fast); }

bool arm_dma_rolling(void*, std::uint16_t* destination, std::uint16_t count) noexcept
{ return arm_dma_backend(destination, count, Backend::Rolling); }

bool arm_dma_oversampled_rolling(void*, std::uint16_t* destination, std::uint16_t count) noexcept
{ return arm_dma_backend(destination, count, Backend::FilteredRolling); }

bool start_trigger(void*) noexcept
{
    if (!context.prepared || !context.dma_armed || context.backend == Backend::None ||
        context.expected_count == 0U || context.error_latch != 0U ||
        (DMA1_Channel1->CCR & DMA_CCR_EN) == 0U ||
        DMA1_Channel1->CNDTR != context.expected_count || !stopped_timer_ready() ||
        (GPIOA->CRL & 0x000000f0U) != 0U) { return false; }
    // Bench Rev X: compare flags with CC2E=0 do not trigger ADC1. Connect the
    // internal OC2 signal while CNT=0/PWM2 is inactive, then start CEN last.
    // GPIO PA1 is still analog input, so this cannot drive the switch-sense net.
    SET_BIT(TIM2->CCER, TIM_CCER_CC2E);
    __DMB();
    SET_BIT(TIM2->CR1, TIM_CR1_CEN);
    return (TIM2->CR1 & TIM_CR1_CEN) != 0U && TIM2->CCER == TIM_CCER_CC2E;
}

DmaObservation observe_backend(bool before, bool cycle_timing) noexcept
{
    const auto first = cycle_timing ? DWT->CYCCNT : 0U;
    std::uint32_t flags_before{};
    std::uint32_t flags_after{};
    std::uint32_t count{};
    bool coherent = false;
    constexpr auto mask = DMA_ISR_HTIF1 | DMA_ISR_TCIF1 | DMA_ISR_TEIF1;
    for (unsigned attempt = 0U; attempt < 3U; ++attempt) {
        flags_before = DMA1->ISR & mask;
        count = DMA1_Channel1->CNDTR;
        flags_after = DMA1->ISR & mask;
        if (flags_before == flags_after) { coherent = true; break; }
    }
    __DMB();
    std::uint8_t flags{};
    if ((flags_after & DMA_ISR_HTIF1) != 0U) { flags |= DmaHalf; }
    if ((flags_after & DMA_ISR_TCIF1) != 0U) { flags |= DmaComplete; }
    if (!coherent || context.error_latch != 0U || (flags_after & DMA_ISR_TEIF1) != 0U) {
        flags |= DmaError;
    }
    const auto enabled = (DMA1_Channel1->CCR & DMA_CCR_EN) != 0U;
    const auto milliseconds = HAL_GetTick();
    const auto last = cycle_timing ? DWT->CYCCNT : 0U;
    return {before ? first : last, milliseconds,
            static_cast<std::uint16_t>(count), flags, enabled};
}

DmaObservation observe(void*, bool before) noexcept
{ return observe_backend(before, true); }

DmaObservation observe_rolling(void*, bool before) noexcept
{ return observe_backend(before, false); }

void acknowledge(void*, std::uint8_t flag) noexcept
{
    // ES096: never clear the global channel flag while the channel is active.
    if (flag == DmaHalf) { DMA1->IFCR = DMA_IFCR_CHTIF1; }
    else if (flag == DmaComplete) { DMA1->IFCR = DMA_IFCR_CTCIF1; }
    else { context.error_latch = 1U; stop_trigger(nullptr); }
    __DMB();
}
}

bool qualify_stm32_acquisition(Stm32AcquisitionQualification value) noexcept
{
    if ((DMA1_Channel1->CCR & DMA_CCR_EN) != 0U || (TIM2->CR1 & TIM_CR1_CEN) != 0U) {
        return false;
    }
    context.qualification = value;
    context.prepared = false;
    context.backend = Backend::None;
    context.expected_count = 0U;
    context.dma_armed = false;
    return true;
}

AcquisitionPort stm32_acquisition_port() noexcept
{
    return {&context, prepare, arm_dma, start_trigger, stop_acquisition, observe, acknowledge};
}

AcquisitionPort stm32_rolling_port() noexcept
{
    return {&context, prepare_rolling, arm_dma_rolling, start_trigger,
            stop_acquisition, observe_rolling, acknowledge};
}

AcquisitionPort stm32_oversampled_rolling_port() noexcept
{
    return {&context, prepare_oversampled_rolling, arm_dma_oversampled_rolling, start_trigger,
            stop_acquisition, observe_rolling, acknowledge};
}

void stm32_acquisition_dma_irq() noexcept
{
    const auto flags = DMA1->ISR;
    if ((flags & DMA_ISR_TEIF1) != 0U) {
        stop_trigger(nullptr);
        context.error_latch = 1U;
        DMA1->IFCR = DMA_IFCR_CTEIF1;
    } else {
        // HT/TC must never interrupt in this backend. Preserve those flags for
        // diagnosis rather than letting HAL consume sole-owner evidence.
        stop_trigger(nullptr);
        context.error_latch = 2U;
        __HAL_DMA_DISABLE_IT(&hdma_adc1, DMA_IT_HT | DMA_IT_TC);
    }
    __DMB();
}

} // namespace dso
