#include "dso_stm32_shared_guard.hpp"
#include "main.h"
#include <cstdint>

namespace dso {

bool stm32_foreground() noexcept
{
    return __get_IPSR() == 0U && __get_PRIMASK() == 0U &&
        __get_BASEPRI() == 0U && __get_FAULTMASK() == 0U;
}

bool stm32_stopped_acquisition() noexcept
{
    // A clock-off peripheral is never accessed. These predicates neither
    // write a peripheral nor take ownership away from its existing driver.
    const auto apb1 = RCC->APB1ENR;
    const auto apb2 = RCC->APB2ENR;
    const auto ahb = RCC->AHBENR;
    return ((apb1 & RCC_APB1ENR_TIM2EN) == 0U || (TIM2->CR1 & TIM_CR1_CEN) == 0U) &&
        ((apb2 & RCC_APB2ENR_ADC1EN) == 0U || (ADC1->CR2 & ADC_CR2_ADON) == 0U) &&
        ((apb2 & RCC_APB2ENR_ADC2EN) == 0U || (ADC2->CR2 & ADC_CR2_ADON) == 0U) &&
        ((ahb & RCC_AHBENR_DMA1EN) == 0U || (DMA1_Channel1->CCR & DMA_CCR_EN) == 0U);
}

bool stm32_stopped_storage_producers() noexcept
{
    const auto apb1 = RCC->APB1ENR;
    const auto apb2 = RCC->APB2ENR;
    const auto ahb = RCC->AHBENR;
    constexpr std::uint32_t TimerRequests = TIM_DIER_UDE | TIM_DIER_CC1DE | TIM_DIER_CC2DE;
    const bool acquisition_off =
        ((apb1 & RCC_APB1ENR_TIM2EN) == 0U || (TIM2->CR1 & TIM_CR1_CEN) == 0U) &&
        ((apb2 & RCC_APB2ENR_ADC1EN) == 0U || (ADC1->CR2 & ADC_CR2_ADON) == 0U) &&
        ((apb2 & RCC_APB2ENR_ADC2EN) == 0U || (ADC2->CR2 & ADC_CR2_ADON) == 0U);
    const bool lcd_timer_off = (apb2 & RCC_APB2ENR_TIM1EN) == 0U ||
        (((TIM1->CR1 & TIM_CR1_CEN) == 0U) && ((TIM1->DIER & TimerRequests) == 0U));
    const bool dma_off = (ahb & RCC_AHBENR_DMA1EN) == 0U ||
        (((DMA1_Channel1->CCR | DMA1_Channel2->CCR | DMA1_Channel3->CCR |
           DMA1_Channel4->CCR | DMA1_Channel5->CCR | DMA1_Channel6->CCR |
           DMA1_Channel7->CCR) & DMA_CCR_EN) == 0U);
    return acquisition_off && lcd_timer_off && dma_off;
}

} // namespace dso
