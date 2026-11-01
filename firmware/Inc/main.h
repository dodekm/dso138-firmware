// Copyright 2026 Martin Dodek
// SPDX-License-Identifier: MIT

#ifndef MAIN_H
#define MAIN_H

#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

extern ADC_HandleTypeDef hadc1;
extern ADC_HandleTypeDef hadc2;
extern DMA_HandleTypeDef hdma_adc1;
extern TIM_HandleTypeDef htim2;

void Error_Handler(void);
void SystemClock_Config(void);

#ifdef __cplusplus
}
#endif

#define BTN_SEL_Pin GPIO_PIN_12
#define BTN_SEL_GPIO_Port GPIOB
#define BTN_MINUS_Pin GPIO_PIN_13
#define BTN_MINUS_GPIO_Port GPIOB
#define BTN_PLUS_Pin GPIO_PIN_14
#define BTN_PLUS_GPIO_Port GPIOB
#define BTN_OK_Pin GPIO_PIN_15
#define BTN_OK_GPIO_Port GPIOB

#endif
