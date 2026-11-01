// Copyright 2026 Martin Dodek
// SPDX-License-Identifier: MIT

#include "main.h"

extern "C" {
extern DMA_HandleTypeDef hdma_adc1;
}

extern "C" void HAL_MspInit()
{
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_AFIO_REMAP_SWJ_NOJTAG();
}

extern "C" void HAL_ADC_MspInit(ADC_HandleTypeDef* adc)
{
    GPIO_InitTypeDef gpio{};
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pin = GPIO_PIN_0;
    if (adc->Instance == ADC1) {
        __HAL_RCC_ADC1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        HAL_GPIO_Init(GPIOA, &gpio);

        hdma_adc1.Instance = DMA1_Channel1;
        hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
        hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
        hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
        hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
        hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
        hdma_adc1.Init.Mode = DMA_CIRCULAR;
        hdma_adc1.Init.Priority = DMA_PRIORITY_VERY_HIGH;
        if (HAL_DMA_Init(&hdma_adc1) != HAL_OK) { Error_Handler(); }
        __HAL_LINKDMA(adc, DMA_Handle, hdma_adc1);
    } else if (adc->Instance == ADC2) {
        __HAL_RCC_ADC2_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        gpio.Pin = GPIO_PIN_1;
        HAL_GPIO_Init(GPIOA, &gpio);
    }
}

extern "C" void HAL_ADC_MspDeInit(ADC_HandleTypeDef* adc)
{
    if (adc->Instance == ADC1) {
        __HAL_RCC_ADC1_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOA, GPIO_PIN_0);
        HAL_DMA_DeInit(adc->DMA_Handle);
    } else if (adc->Instance == ADC2) {
        __HAL_RCC_ADC2_CLK_DISABLE();
        HAL_GPIO_DeInit(GPIOA, GPIO_PIN_1);
    }
}

extern "C" void HAL_TIM_Base_MspInit(TIM_HandleTypeDef* timer)
{
    if (timer->Instance == TIM2) { __HAL_RCC_TIM2_CLK_ENABLE(); }
}

extern "C" void HAL_TIM_Base_MspDeInit(TIM_HandleTypeDef* timer)
{
    if (timer->Instance == TIM2) { __HAL_RCC_TIM2_CLK_DISABLE(); }
}
