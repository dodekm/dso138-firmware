#ifndef DSO_STM32_LCD_DMA_HPP
#define DSO_STM32_LCD_DMA_HPP

#include "dso_display.hpp"
#include <cstdint>

namespace dso {
enum class Stm32LcdDmaFault : std::uint8_t {
    None, Qualification, Execution, Clocks, Ownership, Configuration, Transfer, Timing, Stop
};
// Explicit UID-qualified owner claims the otherwise unused TIM1 and DMA1
// channels2/3/5. Preparation drives no GPIO and enables no DMA channel.
// Initial false permission is RAM-only; revocation of an owner aborts first.
[[nodiscard]] bool prepare_stm32_lcd_dma(bool permission) noexcept;
[[nodiscard]] bool stm32_lcd_dma_ready() noexcept;
// Requires selected LCD, D/C=data, WR/RD/RESET high and immutable GPIO ownership.
// Copies <=DisplayDmaPixels RGB565 words to static masked BSRR storage before
// returning. The DSO138 target uses 32; the generic module default is 16.
[[nodiscard]] bool stm32_lcd_dma_start(const std::uint16_t* source, std::uint16_t count) noexcept;
[[nodiscard]] TransferState stm32_lcd_dma_poll() noexcept;
// Synchronous cancellation: stops requests, counter and all three DMA readers.
// A failed stop is terminal; it cannot return authority to a subsequent window.
void stm32_lcd_dma_abort() noexcept;
// IRQ clears/stops the bounded engines and publishes flags/time only. No bus
// callbacks, source accesses, framebuffer work or GPIO writes occur here.
void stm32_lcd_dma_irq() noexcept;
[[nodiscard]] Stm32LcdDmaFault stm32_lcd_dma_fault() noexcept;
} // namespace dso
#endif
