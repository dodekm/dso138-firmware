#include "dso_stm32_adc.hpp"
#include <cstdint>

namespace dso {
namespace {
constexpr std::uint32_t TimeoutCycles{72000U};
constexpr unsigned MaximumIterations{16384U};

bool delay_cycles(std::uint32_t cycles) noexcept
{
    const auto start = DWT->CYCCNT;
    for (unsigned i = 0U; i < MaximumIterations; ++i) {
        if (static_cast<std::uint32_t>(DWT->CYCCNT - start) >= cycles) { return true; }
        __NOP();
    }
    return false;
}

bool wait_clear(volatile std::uint32_t& reg, std::uint32_t mask) noexcept
{
    const auto start = DWT->CYCCNT;
    for (unsigned i = 0U; i < MaximumIterations; ++i) {
        if ((reg & mask) == 0U) { return true; }
        if (static_cast<std::uint32_t>(DWT->CYCCNT - start) >= TimeoutCycles) { return false; }
    }
    return false;
}
}

bool calibrate_stm32_adc(ADC_HandleTypeDef& adc) noexcept
{
    if ((adc.Instance != ADC1 && adc.Instance != ADC2) || adc.Lock != HAL_UNLOCKED ||
        __get_IPSR() != 0U || __get_PRIMASK() != 0U || __get_BASEPRI() != 0U || __get_FAULTMASK() != 0U ||
        HAL_RCC_GetHCLKFreq() != 72000000U ||
        HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_ADC) != 12000000U ||
        ((ADC1->CR2 | ADC2->CR2) & ADC_CR2_ADON) != 0U ||
        (adc.Instance->CR2 & (ADC_CR2_DMA | ADC_CR2_EXTTRIG | ADC_CR2_JEXTTRIG |
                              ADC_CR2_CAL | ADC_CR2_RSTCAL)) != 0U) { return false; }
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    // Also detects a missing/stopped cycle counter through the iteration bound.
    if (!delay_cycles(16U)) { return false; } // >=2 ADCCLK disabled at /6.
    adc.Lock = HAL_LOCKED;
    SET_BIT(adc.Instance->CR2, ADC_CR2_ADON);
    bool success = (adc.Instance->CR2 & ADC_CR2_ADON) != 0U && delay_cycles(72U);
    if (success) {
        SET_BIT(adc.Instance->CR2, ADC_CR2_RSTCAL);
        success = wait_clear(adc.Instance->CR2, ADC_CR2_RSTCAL);
    }
    if (success) {
        SET_BIT(adc.Instance->CR2, ADC_CR2_CAL);
        success = wait_clear(adc.Instance->CR2, ADC_CR2_CAL);
    }
    CLEAR_BIT(adc.Instance->CR2, ADC_CR2_ADON);
    __DSB();
    success = success && (adc.Instance->CR2 & ADC_CR2_ADON) == 0U;
    adc.State = success ? HAL_ADC_STATE_READY : HAL_ADC_STATE_ERROR_INTERNAL;
    adc.ErrorCode = success ? HAL_ADC_ERROR_NONE : HAL_ADC_ERROR_INTERNAL;
    adc.Lock = HAL_UNLOCKED;
    return success;
}

bool enable_stm32_adc_bounded(ADC_HandleTypeDef& adc) noexcept
{
    if ((adc.Instance != ADC1 && adc.Instance != ADC2) || adc.Lock != HAL_UNLOCKED ||
        __get_IPSR() != 0U ||
        (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U ||
        HAL_RCC_GetHCLKFreq() != 72000000U ||
        (adc.Instance->CR2 & (ADC_CR2_ADON | ADC_CR2_CAL | ADC_CR2_RSTCAL)) != 0U) { return false; }
    SET_BIT(adc.Instance->CR2, ADC_CR2_ADON);
    if ((adc.Instance->CR2 & ADC_CR2_ADON) != 0U && delay_cycles(72U)) { return true; }
    CLEAR_BIT(adc.Instance->CR2, ADC_CR2_ADON);
    adc.State = HAL_ADC_STATE_ERROR_INTERNAL;
    adc.ErrorCode = HAL_ADC_ERROR_INTERNAL;
    return false;
}
} // namespace dso
