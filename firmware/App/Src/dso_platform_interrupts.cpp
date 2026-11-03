// Copyright 2026 Martin Dodek
// SPDX-License-Identifier: MIT

#include "main.h"
#include "dso_app.h"

extern "C" {
extern DMA_HandleTypeDef hdma_adc1;
}

extern "C" void SysTick_Handler()
{
    HAL_IncTick();
}

extern "C" void DMA1_Channel1_IRQHandler()
{
    if (dso_app_dma_irq() != 0) { return; }
    HAL_DMA_IRQHandler(&hdma_adc1);
}

extern "C" void DMA1_Channel2_IRQHandler()
{
    dso_app_lcd_dma_irq();
}

extern "C" void DMA1_Channel3_IRQHandler()
{
    dso_app_lcd_dma_irq();
}

extern "C" void DMA1_Channel5_IRQHandler()
{
    dso_app_lcd_dma_irq();
}

#if defined(DSO_BOARD_USB) && DSO_BOARD_USB
extern "C" void USB_LP_CAN1_RX0_IRQHandler()
{
    dso_app_usb_irq();
}
#endif
