#ifndef DSO_STM32_BOARD_CLOCK_HPP
#define DSO_STM32_BOARD_CLOCK_HPP

#include "main.h"
#include <cstdint>

namespace dso::detail {

// The board's generated clock contract is an 8 MHz HSE, PLL x9, SYSCLK/HCLK/
// PCLK2 at 72 MHz, with APB2 undivided. The LCD bus and its timer depend on it.
static_assert(HSE_VALUE == 8000000U, "LCD clock predicate assumes the board's 8 MHz HSE");

[[nodiscard]] inline bool lcd_clock_tree_matches() noexcept
{
    constexpr std::uint32_t Ready = RCC_CR_HSEON | RCC_CR_HSERDY | RCC_CR_PLLON | RCC_CR_PLLRDY;
    constexpr std::uint32_t Fields = RCC_CFGR_SW | RCC_CFGR_SWS | RCC_CFGR_HPRE |
        RCC_CFGR_PPRE2 | RCC_CFGR_PLLSRC | RCC_CFGR_PLLXTPRE | RCC_CFGR_PLLMULL;
    constexpr std::uint32_t Expected = RCC_CFGR_SW_PLL | RCC_CFGR_SWS_PLL |
        RCC_CFGR_PLLSRC | RCC_CFGR_PLLMULL9;
    const auto cr = RCC->CR;
    const auto cfgr = RCC->CFGR;
    return SystemCoreClock == 72000000U && (cr & Ready) == Ready &&
        (cr & RCC_CR_HSEBYP) == 0U && (cfgr & Fields) == Expected;
}

} // namespace dso::detail
#endif
