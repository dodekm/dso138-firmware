// Copyright 2026 Martin Dodek
// SPDX-License-Identifier: MIT

#ifndef STM32F1XX_IT_H
#define STM32F1XX_IT_H

#ifdef __cplusplus
extern "C" {
#endif

void SysTick_Handler(void);
void DMA1_Channel1_IRQHandler(void);
void DMA1_Channel2_IRQHandler(void);
void DMA1_Channel3_IRQHandler(void);
void DMA1_Channel5_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif
