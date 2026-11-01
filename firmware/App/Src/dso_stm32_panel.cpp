#include "dso_stm32_panel.hpp"
#include "dso_stm32_adc.hpp"
#include "main.h"

#include <cstdint>

extern "C" {
extern ADC_HandleTypeDef hadc2;
}

namespace dso {
namespace {

struct BindingState final {
    bool prepared{false};
    bool active{false};
    bool owns_adc{false};
    bool pins_verified{false};
    bool failed{false};
};

BindingState binding;

constexpr std::uint32_t StateErrors = HAL_ADC_STATE_TIMEOUT |
    HAL_ADC_STATE_ERROR_INTERNAL | HAL_ADC_STATE_ERROR_CONFIG |
    HAL_ADC_STATE_ERROR_DMA | HAL_ADC_STATE_BUSY_INTERNAL |
    HAL_ADC_STATE_INJ_BUSY | HAL_ADC_STATE_MULTIMODE_SLAVE;
constexpr std::uint32_t ForbiddenCr1 = ADC_CR1_SCAN | ADC_CR1_DISCEN |
    ADC_CR1_JAUTO | ADC_CR1_JDISCEN | ADC_CR1_EOCIE | ADC_CR1_JEOCIE | ADC_CR1_AWDIE;
constexpr std::uint32_t ForbiddenCr2 = ADC_CR2_CONT | ADC_CR2_DMA | ADC_CR2_ALIGN |
    ADC_CR2_JEXTTRIG | ADC_CR2_JSWSTART | ADC_CR2_CAL | ADC_CR2_RSTCAL;

bool foreground_unmasked() noexcept
{
    return __get_IPSR() == 0U && __get_PRIMASK() == 0U && __get_BASEPRI() == 0U &&
        __get_FAULTMASK() == 0U;
}

bool independent_adc_clocks() noexcept
{
    return hadc2.Instance == ADC2 &&
        (RCC->APB2ENR & (RCC_APB2ENR_ADC1EN | RCC_APB2ENR_ADC2EN)) ==
        (RCC_APB2ENR_ADC1EN | RCC_APB2ENR_ADC2EN) &&
        (ADC1->CR1 & ADC_CR1_DUALMOD) == 0U;
}

bool button_inputs_configured() noexcept
{
    return (RCC->APB2ENR & RCC_APB2ENR_IOPBEN) != 0U &&
        (GPIOB->CRH & 0xffff0000U) == 0x88880000U &&
        (GPIOB->ODR & 0xf000U) == 0xf000U;
}

bool analog_inputs_configured() noexcept
{
    return (RCC->APB2ENR & RCC_APB2ENR_IOPAEN) != 0U &&
        (GPIOA->CRL & 0x0000fff0U) == 0U;
}

bool mode_configured() noexcept
{
    return independent_adc_clocks() && hadc2.Lock == HAL_UNLOCKED &&
        hadc2.Init.ScanConvMode == ADC_SCAN_DISABLE &&
        hadc2.Init.ContinuousConvMode == DISABLE &&
        hadc2.Init.DiscontinuousConvMode == DISABLE &&
        hadc2.Init.ExternalTrigConv == ADC_SOFTWARE_START &&
        hadc2.Init.DataAlign == ADC_DATAALIGN_RIGHT &&
        hadc2.Init.NbrOfConversion == 1U && hadc2.DMA_Handle == nullptr &&
        hadc2.ErrorCode == HAL_ADC_ERROR_NONE && (hadc2.State & StateErrors) == 0U &&
        (ADC2->CR1 & ForbiddenCr1) == 0U &&
        (ADC2->CR2 & ForbiddenCr2) == 0U &&
        (ADC2->CR2 & ADC_CR2_EXTSEL) == ADC_SOFTWARE_START &&
        (ADC2->SQR1 & ADC_SQR1_L) == 0U &&
        HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_ADC) == 12000000U;
}

void fail_binding() noexcept
{
    binding.prepared = false;
    binding.failed = true;
}

bool stop_adc2_bounded() noexcept
{
    if (!independent_adc_clocks()) {
        fail_binding();
        return false;
    }
    // RM0008 ADC_CR2.ADON=0 disables ADC2 and aborts its conversion. HAL_Stop
    // normally performs this write then polls ADON using SysTick. A one-shot
    // readback avoids that blocking fault path in the foreground DMA budget.
    // Only ADC2 registers and its exclusively owned HAL handle are changed.
    CLEAR_BIT(ADC2->CR2, ADC_CR2_ADON);
    __DSB();
    if ((ADC2->CR2 & ADC_CR2_ADON) != 0U) {
        SET_BIT(hadc2.State, HAL_ADC_STATE_ERROR_INTERNAL);
        SET_BIT(hadc2.ErrorCode, HAL_ADC_ERROR_INTERNAL);
        fail_binding();
        return false;
    }
    CLEAR_BIT(ADC2->CR2, ADC_CR2_EXTTRIG | ADC_CR2_SWSTART);
    __HAL_ADC_CLEAR_FLAG(&hadc2, ADC_FLAG_STRT | ADC_FLAG_EOC);
    ADC_STATE_CLR_SET(hadc2.State,
        HAL_ADC_STATE_REG_BUSY | HAL_ADC_STATE_INJ_BUSY | HAL_ADC_STATE_REG_EOC,
        HAL_ADC_STATE_READY);
    binding.active = false;
    return true;
}

bool start_conversion(void* context, std::uint8_t channel) noexcept
{
    if (context != &binding || !binding.prepared || binding.failed || binding.active ||
        channel < 1U || channel > 3U) {
        return false;
    }
    if (!foreground_unmasked() || !mode_configured() || !analog_inputs_configured() ||
        !button_inputs_configured() || (ADC2->CR2 & ADC_CR2_ADON) != 0U) {
        fail_binding();
        return false;
    }
    ADC_ChannelConfTypeDef config{};
    config.Channel = channel == 1U ? ADC_CHANNEL_1 :
        channel == 2U ? ADC_CHANNEL_2 : ADC_CHANNEL_3;
    config.Rank = ADC_REGULAR_RANK_1;
    config.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    if (HAL_ADC_ConfigChannel(&hadc2, &config) != HAL_OK) {
        fail_binding();
        return false;
    }
    // Pre-enable with the shared DWT/iteration bound. HAL_ADC_Start then sees
    // known-enabled ADC2 and skips ADC_Enable's SysTick-based failure wait.
    // Exclusive ownership forbids a competing ADON clear between these calls.
    binding.active = true; // A failed start still requires bounded cleanup.
    if (!enable_stm32_adc_bounded(hadc2) || HAL_ADC_Start(&hadc2) != HAL_OK) {
        (void)stop_adc2_bounded();
        fail_binding();
        return false;
    }
    return true;
}

PanelPollResult poll_conversion(void* context) noexcept
{
    if (context != &binding || !binding.prepared || binding.failed || !binding.active) {
        return {PanelPollState::Fault, 0U};
    }
    if (!mode_configured() || (ADC2->CR2 & ADC_CR2_ADON) == 0U) {
        fail_binding();
        return {PanelPollState::Fault, 0U};
    }
    if (__HAL_ADC_GET_FLAG(&hadc2, ADC_FLAG_EOC) == RESET) {
        return {PanelPollState::Pending, 0U};
    }
    const auto value = HAL_ADC_GetValue(&hadc2); // Reading DR consumes EOC.
    if (!stop_adc2_bounded() || value > 4095U) {
        fail_binding();
        return {PanelPollState::Fault, 0U};
    }
    return {PanelPollState::Ready, static_cast<std::uint16_t>(value)};
}

void abort_conversion(void* context) noexcept
{
    if (context == &binding && binding.owns_adc) {
        (void)stop_adc2_bounded();
    }
}

std::uint16_t read_buttons(void* context) noexcept
{
    // ADC faults need not suppress the independently valid GPIO button scan.
    if (context == &binding && binding.pins_verified && button_inputs_configured()) {
        return static_cast<std::uint16_t>(GPIOB->IDR);
    }
    return 0xffffU; // No unqualified MMIO access; caller must prepare before use.
}

} // namespace

bool prepare_stm32_panel(bool pinmap_verified) noexcept
{
    binding.prepared = false;
    binding.pins_verified = false;
    if (!pinmap_verified) {
        return false; // Explicitly no hardware read/write/start on this path.
    }
    if (!foreground_unmasked() || !mode_configured() || binding.active ||
        !analog_inputs_configured() || !button_inputs_configured() ||
        (ADC1->CR2 & ADC_CR2_ADON) != 0U || (ADC2->CR2 & ADC_CR2_ADON) != 0U ||
        (hadc2.State & HAL_ADC_STATE_READY) == 0U || SystemCoreClock != 72000000U) {
        fail_binding();
        return false;
    }
    binding.pins_verified = true;
    // Calibration is startup-only while waveform acquisition is stopped.
    // The shared helper checks DWT deadlines AND fixed iteration ceilings.
    binding.owns_adc = true;
    CLEAR_BIT(ADC2->CR2, ADC_CR2_EXTTRIG | ADC_CR2_SWSTART);
    if (!calibrate_stm32_adc(hadc2)) {
        (void)stop_adc2_bounded();
        fail_binding();
        return false;
    }
    if (!stop_adc2_bounded() || !mode_configured()) {
        fail_binding();
        return false;
    }
    binding.failed = false;
    binding.prepared = true;
    return true;
}

PanelSamplerPort stm32_panel_port() noexcept
{
    return PanelSamplerPort{&binding, start_conversion, poll_conversion,
        abort_conversion, read_buttons};
}

} // namespace dso
