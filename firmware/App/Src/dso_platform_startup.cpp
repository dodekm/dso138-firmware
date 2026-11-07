// Copyright 2026 Martin Dodek
// SPDX-License-Identifier: MIT

#include "main.h"
#include "dso_app.h"

extern "C" {
ADC_HandleTypeDef hadc1{};
ADC_HandleTypeDef hadc2{};
DMA_HandleTypeDef hdma_adc1{};
TIM_HandleTypeDef htim2{};
}

namespace {

void gpio_init()
{
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio{};
    gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    gpio.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = BTN_SEL_Pin | BTN_MINUS_Pin | BTN_PLUS_Pin | BTN_OK_Pin;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &gpio);
}

void dma_init()
{
    __HAL_RCC_DMA1_CLK_ENABLE();
    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 1U, 0U);
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
}

void adc1_init()
{
    hadc1.Instance = ADC1;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIGCONV_T2_CC2;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1U;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) { Error_Handler(); }

    ADC_ChannelConfTypeDef channel{};
    channel.Channel = ADC_CHANNEL_0;
    channel.Rank = ADC_REGULAR_RANK_1;
    channel.SamplingTime = ADC_SAMPLETIME_7CYCLES_5;
    if (HAL_ADC_ConfigChannel(&hadc1, &channel) != HAL_OK) { Error_Handler(); }
}

void tim2_init()
{
    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 0U;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 143U;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim2) != HAL_OK) { Error_Handler(); }

    TIM_ClockConfigTypeDef clock{};
    clock.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim2, &clock) != HAL_OK) { Error_Handler(); }

    TIM_MasterConfigTypeDef master{};
    master.MasterOutputTrigger = TIM_TRGO_OC2REF;
    master.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &master) != HAL_OK) { Error_Handler(); }
}

void adc2_init()
{
    hadc2.Instance = ADC2;
    hadc2.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc2.Init.ContinuousConvMode = DISABLE;
    hadc2.Init.DiscontinuousConvMode = DISABLE;
    hadc2.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc2.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc2.Init.NbrOfConversion = 1U;
    if (HAL_ADC_Init(&hadc2) != HAL_OK) { Error_Handler(); }

    ADC_ChannelConfTypeDef channel{};
    channel.Channel = ADC_CHANNEL_1;
    channel.Rank = ADC_REGULAR_RANK_1;
    channel.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    if (HAL_ADC_ConfigChannel(&hadc2, &channel) != HAL_OK) { Error_Handler(); }
}

}

extern "C" int main()
{
    if (dso_app_prepare_core() == 0U) { Error_Handler(); }
    HAL_Init();
    SystemClock_Config();
    gpio_init();
    dma_init();
    adc1_init();
    tim2_init();
    adc2_init();
    dso_app_init();
    for (;;) { dso_app_idle(); }
}

extern "C" void Error_Handler()
{
    dso_app_fault(static_cast<uint32_t>(DSO_APP_FAULT_HAL_ERROR_HANDLER));
    __disable_irq();
    for (;;) { }
}

#ifdef USE_FULL_ASSERT
extern "C" void assert_failed(uint8_t* file, uint32_t line)
{
    static_cast<void>(file);
    static_cast<void>(line);
}
#endif
