/* Copyright 2026 Martin Dodek. SPDX-License-Identifier: MIT
 * Project-selected HAL capabilities and clock constants for the classic DSO138.
 * The HAL implementation and its own headers retain their ST license.
 */
#ifndef DSO_HAL_CONFIGURATION_H
#define DSO_HAL_CONFIGURATION_H
#define HAL_MODULE_ENABLED
#define HAL_ADC_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED
#ifndef HSE_VALUE
#define HSE_VALUE 8000000U
#endif
#ifndef HSI_VALUE
#define HSI_VALUE 8000000U
#endif
#ifndef LSI_VALUE
#define LSI_VALUE 40000U
#endif
#ifndef LSE_VALUE
#define LSE_VALUE 32768U
#endif
#ifndef HSE_STARTUP_TIMEOUT
#define HSE_STARTUP_TIMEOUT 100U
#endif
#ifndef LSE_STARTUP_TIMEOUT
#define LSE_STARTUP_TIMEOUT 5000U
#endif
#define VDD_VALUE 3300U
#define TICK_INT_PRIORITY 15U
#define USE_RTOS 0U
#define PREFETCH_ENABLE 1U
#define USE_HAL_ADC_REGISTER_CALLBACKS 0U
#define USE_HAL_TIM_REGISTER_CALLBACKS 0U
#include "stm32f1xx_hal_rcc.h"
#include "stm32f1xx_hal_gpio.h"
#include "stm32f1xx_hal_exti.h"
#include "stm32f1xx_hal_dma.h"
#include "stm32f1xx_hal_cortex.h"
#include "stm32f1xx_hal_adc.h"
#include "stm32f1xx_hal_flash.h"
#include "stm32f1xx_hal_pwr.h"
#include "stm32f1xx_hal_tim.h"
#ifdef USE_FULL_ASSERT
#ifdef __cplusplus
extern "C" {
#endif
void assert_failed(uint8_t *file, uint32_t line);
#ifdef __cplusplus
}
#endif
#define assert_param(condition) ((condition) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
#else
#define assert_param(condition) ((void)0U)
#endif
#endif
