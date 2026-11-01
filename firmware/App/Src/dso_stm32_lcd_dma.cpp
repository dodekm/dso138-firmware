#include "dso_stm32_lcd_dma.hpp"
#include "dso_stm32_board_clock.hpp"
#include "dso_stm32_shared_guard.hpp"
#include "main.h"
#include <cstdint>

extern "C" [[noreturn]] void dso_software_fault_entry(void);

namespace dso {
namespace {
constexpr std::uint32_t DataMask{0xffU}, ReadReset{0xc00U};
constexpr std::uint32_t Select{0x2000U}, Data{0x4000U}, Write{0x8000U};
constexpr std::uint32_t Flags{DMA_IFCR_CGIF2 | DMA_IFCR_CGIF3 | DMA_IFCR_CGIF5};
constexpr std::uint32_t Errors{DMA_ISR_TEIF2 | DMA_ISR_TEIF3 | DMA_ISR_TEIF5};
constexpr std::uint32_t Requests{TIM_DIER_UDE | TIM_DIER_CC1DE | TIM_DIER_CC2DE};
constexpr std::uint32_t Interrupts{DMA_CCR_TCIE | DMA_CCR_HTIE | DMA_CCR_TEIE};
constexpr std::uint32_t Common{DMA_CCR_DIR | DMA_CCR_PSIZE_1 | DMA_CCR_MSIZE_1 | DMA_CCR_TEIE};
constexpr std::uint32_t HalfRange{0x80000000U}, HoldCycles{36U}, TimeoutCycles{72000U};
struct Context final {
    alignas(4) std::uint32_t words[2U * DisplayDmaPixels]{};
    std::uint32_t low{}, high{};
    std::uint32_t started{};
    volatile std::uint32_t flags{}, ended{};
    std::uint16_t bytes{}, polls{};
    volatile TransferState state{TransferState::Idle};
    volatile Stm32LcdDmaFault fault{Stm32LcdDmaFault::None};
    volatile bool owned{}, ended_valid{};
};
Context context;
static_assert(sizeof(Context) <= 2U*DisplayDmaPixels*sizeof(std::uint32_t) + 32U,
              "LCD DMA context permits the byte-strobe buffer plus 32 bytes of metadata");

bool clocks() noexcept
{
    return (RCC->AHBENR & RCC_AHBENR_DMA1EN) != 0U &&
        (RCC->APB2ENR & (RCC_APB2ENR_TIM1EN | RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPCEN)) ==
        (RCC_APB2ENR_TIM1EN | RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPCEN) &&
        (RCC->APB2RSTR & (RCC_APB2RSTR_TIM1RST | RCC_APB2RSTR_IOPBRST | RCC_APB2RSTR_IOPCRST)) == 0U;
}

bool pins() noexcept
{
    return GPIOB->CRL == 0x22222222U && (GPIOB->CRH & 0xff00U) == 0x2200U &&
        (GPIOC->CRH & 0xfff00000U) == 0x22200000U;
}

bool other_requests_off() noexcept
{
    // RM0008 figure50: requests are ORed, not dynamically multiplexed. Never
    // read a clock-gated peripheral merely to inspect stale DMA-enable bits.
    const auto apb1 = RCC->APB1ENR;
    const auto apb2 = RCC->APB2ENR;
    return ((apb1 & RCC_APB1ENR_TIM2EN) == 0U || (TIM2->DIER & (TIM_DIER_UDE | TIM_DIER_CC1DE)) == 0U) &&
        ((apb1 & RCC_APB1ENR_TIM3EN) == 0U || (TIM3->DIER & (TIM_DIER_UDE | TIM_DIER_CC3DE | TIM_DIER_CC4DE)) == 0U) &&
        ((apb1 & RCC_APB1ENR_TIM4EN) == 0U || (TIM4->DIER & TIM_DIER_CC3DE) == 0U) &&
        ((apb1 & RCC_APB1ENR_USART3EN) == 0U || (USART3->CR3 & (USART_CR3_DMAT | USART_CR3_DMAR)) == 0U) &&
        ((apb2 & RCC_APB2ENR_USART1EN) == 0U || (USART1->CR3 & USART_CR3_DMAR) == 0U) &&
        ((apb2 & RCC_APB2ENR_SPI1EN) == 0U || (SPI1->CR2 & (SPI_CR2_RXDMAEN | SPI_CR2_TXDMAEN)) == 0U) &&
        ((apb1 & RCC_APB1ENR_SPI2EN) == 0U || (SPI2->CR2 & SPI_CR2_TXDMAEN) == 0U) &&
        ((apb1 & RCC_APB1ENR_I2C2EN) == 0U || (I2C2->CR2 & I2C_CR2_DMAEN) == 0U);
}

bool channels_off() noexcept
{
    return ((DMA1_Channel2->CCR | DMA1_Channel3->CCR | DMA1_Channel5->CCR) & DMA_CCR_EN) == 0U;
}

void stop_engines() noexcept
{
    // A source cannot be reused after a hopeful stop. Failure transfers control
    // directly to the stackless terminal handler, never through app callbacks.
    if (!clocks()) { context.fault = Stm32LcdDmaFault::Stop; dso_software_fault_entry(); }
    TIM1->DIER = 0U;
    CLEAR_BIT(TIM1->CR1,TIM_CR1_CEN);
    CLEAR_BIT(DMA1_Channel2->CCR,DMA_CCR_EN | Interrupts);
    CLEAR_BIT(DMA1_Channel3->CCR,DMA_CCR_EN | Interrupts);
    CLEAR_BIT(DMA1_Channel5->CCR,DMA_CCR_EN | Interrupts);
    __DSB();
    if (TIM1->DIER != 0U || (TIM1->CR1 & TIM_CR1_CEN) != 0U || !channels_off()) {
        context.fault = Stm32LcdDmaFault::Stop; dso_software_fault_entry();
    }
    DMA1->IFCR = Flags; // Own channels only, after all readers are disabled.
    __DSB();
}

bool fail(Stm32LcdDmaFault fault) noexcept
{
    if (context.owned && context.state == TransferState::Pending) { stop_engines(); }
    if (context.fault == Stm32LcdDmaFault::None) { context.fault = fault; }
    context.state = TransferState::Fault;
    return false;
}

void clear_pending() noexcept
{
    HAL_NVIC_ClearPendingIRQ(DMA1_Channel2_IRQn);
    HAL_NVIC_ClearPendingIRQ(DMA1_Channel3_IRQn);
    HAL_NVIC_ClearPendingIRQ(DMA1_Channel5_IRQn);
}

bool timer_configured() noexcept
{
    return TIM1->CR1 == TIM_CR1_URS && TIM1->CR2 == 0U && TIM1->SMCR == 0U && TIM1->DIER == 0U &&
        TIM1->CCMR1 == 0U && TIM1->CCMR2 == 0U && TIM1->CCER == 0U && TIM1->BDTR == 0U &&
        TIM1->PSC == 0U && TIM1->ARR == 215U && TIM1->RCR == 0U && TIM1->CCR1 == 72U && TIM1->CCR2 == 144U;
}

void channel(DMA_Channel_TypeDef* port, volatile std::uint32_t* destination,
             const std::uint32_t* source, std::uint16_t count, std::uint32_t options) noexcept
{
    port->CCR = 0U;
    port->CPAR = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(destination));
    port->CMAR = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(source));
    port->CNDTR = count;
    port->CCR = Common | options;
}

bool channel_matches(DMA_Channel_TypeDef* port, volatile std::uint32_t* destination,
                     const std::uint32_t* source, std::uint16_t count, std::uint32_t options) noexcept
{
    return port->CCR == (Common | options) && port->CNDTR == count &&
        port->CPAR == static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(destination)) &&
        port->CMAR == static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(source));
}
} // namespace

bool prepare_stm32_lcd_dma(bool permission) noexcept
{
    if (!permission) {
        if (context.owned) { stm32_lcd_dma_abort(); }
        context.owned = false; context.state = TransferState::Idle; context.fault = Stm32LcdDmaFault::Qualification;
        return false;
    }
    if (context.state == TransferState::Pending) { return false; }
    context.fault = Stm32LcdDmaFault::None;
    if (!stm32_foreground()) { return fail(Stm32LcdDmaFault::Execution); }
    if (!detail::lcd_clock_tree_matches() ||
        (RCC->AHBENR & RCC_AHBENR_DMA1EN) == 0U || (RCC->APB2RSTR & RCC_APB2RSTR_TIM1RST) != 0U) {
        return fail(Stm32LcdDmaFault::Clocks);
    }
    if (!stm32_stopped_acquisition() || !other_requests_off() || !channels_off()) { return fail(Stm32LcdDmaFault::Ownership); }
    if ((RCC->APB2ENR & RCC_APB2ENR_TIM1EN) != 0U) {
        if ((TIM1->CR1 & TIM_CR1_CEN) != 0U || TIM1->CR2 != 0U || TIM1->SMCR != 0U ||
            TIM1->DIER != 0U || TIM1->CCER != 0U || TIM1->BDTR != 0U) {
            return fail(Stm32LcdDmaFault::Ownership);
        }
    } else {
        // Explicit ownership is required before resetting a clock-gated TIM1;
        // never resume an unknown stale counter/DMA request by enabling it.
        SET_BIT(RCC->APB2RSTR,RCC_APB2RSTR_TIM1RST);
        CLEAR_BIT(RCC->APB2RSTR,RCC_APB2RSTR_TIM1RST);
        SET_BIT(RCC->APB2ENR,RCC_APB2ENR_TIM1EN);
    }
    if (!clocks() || !pins()) { return fail(Stm32LcdDmaFault::Configuration); }
    CLEAR_BIT(DMA1_Channel2->CCR,Interrupts);
    CLEAR_BIT(DMA1_Channel3->CCR,Interrupts);
    CLEAR_BIT(DMA1_Channel5->CCR,Interrupts);
    DMA1->IFCR = Flags;
    TIM1->DIER = 0U; TIM1->CR1 = TIM_CR1_URS; TIM1->CR2 = TIM_TRGO_ENABLE; TIM1->SMCR = 0U;
    TIM1->CCMR1 = 0U; TIM1->CCMR2 = 0U; TIM1->CCER = 0U; TIM1->BDTR = 0U;
    TIM1->PSC = 0U; TIM1->ARR = 215U; TIM1->RCR = 0U; TIM1->CCR1 = 72U; TIM1->CCR2 = 144U;
    // MMS=Enable holds TRGO inactive while CEN=0; MMS=Reset would export UG
    // to any future slave despite disabled DMA/interrupt request bits.
    TIM1->EGR = TIM_EGR_UG; TIM1->SR = 0U; TIM1->CNT = 0U; TIM1->CR2 = 0U;
    __DSB();
    if (!timer_configured() || (CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0U ||
        (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U) { return fail(Stm32LcdDmaFault::Configuration); }
    context.flags = 0U; context.ended_valid = false; context.state = TransferState::Idle;
    context.low = Write << 16U; context.high = Write;
    context.fault = Stm32LcdDmaFault::None; context.owned = true;
    clear_pending();
    HAL_NVIC_SetPriority(DMA1_Channel2_IRQn,2U,0U);
    HAL_NVIC_SetPriority(DMA1_Channel3_IRQn,2U,0U);
    HAL_NVIC_SetPriority(DMA1_Channel5_IRQn,2U,0U);
    HAL_NVIC_EnableIRQ(DMA1_Channel2_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);
    return true;
}

bool stm32_lcd_dma_ready() noexcept
{
    return context.owned && context.fault == Stm32LcdDmaFault::None && context.state != TransferState::Pending &&
        stm32_foreground() && clocks() && pins() && timer_configured() && channels_off() && other_requests_off();
}

bool stm32_lcd_dma_start(const std::uint16_t* source, std::uint16_t count) noexcept
{
    if (source == nullptr || (reinterpret_cast<std::uintptr_t>(source) & 1U) != 0U || count == 0U || count > DisplayDmaPixels) { return false; }
    if (!stm32_lcd_dma_ready()) { return false; }
    if (context.low != (Write << 16U) || context.high != Write ||
        (GPIOB->ODR & ReadReset) != ReadReset || (GPIOC->ODR & (Select | Data | Write)) != (Data | Write) ||
        ((DMA1_Channel1->CCR & DMA_CCR_EN) != 0U && (DMA1_Channel1->CCR & DMA_CCR_PL) != DMA_CCR_PL)) {
        return fail(Stm32LcdDmaFault::Configuration);
    }
    for (std::uint16_t i{}; i < count; ++i) {
        const auto value = source[i];
        const auto high = std::uint32_t{value} >> 8U;
        const auto low = std::uint32_t{value & DataMask};
        context.words[2U*i] = high | ((~high & DataMask) << 16U);
        context.words[2U*i+1U] = low | ((~low & DataMask) << 16U);
    }
    const auto bytes = static_cast<std::uint16_t>(2U*count);
    // TIM1_UP→CH5 loads the next byte, CC1→CH2 drives WR low, CC2→CH3 high.
    // Priorities preserve that order if requests coincide, below ADC1 VeryHigh.
    DMA1->IFCR = Flags; // Clear stale level flags before setting any IRQ enables.
    clear_pending();
    channel(DMA1_Channel5,&GPIOB->BSRR,context.words+1U,static_cast<std::uint16_t>(bytes-1U),DMA_CCR_MINC | DMA_CCR_PL_1);
    channel(DMA1_Channel2,&GPIOC->BSRR,&context.low,bytes,DMA_CCR_PL_0);
    channel(DMA1_Channel3,&GPIOC->BSRR,&context.high,bytes,DMA_CCR_TCIE);
    if (!channel_matches(DMA1_Channel5,&GPIOB->BSRR,context.words+1U,static_cast<std::uint16_t>(bytes-1U),DMA_CCR_MINC | DMA_CCR_PL_1) ||
        !channel_matches(DMA1_Channel2,&GPIOC->BSRR,&context.low,bytes,DMA_CCR_PL_0) ||
        !channel_matches(DMA1_Channel3,&GPIOC->BSRR,&context.high,bytes,DMA_CCR_TCIE)) { return fail(Stm32LcdDmaFault::Configuration); }
    TIM1->CNT = 0U; TIM1->SR = 0U;
    __DSB();
    if (TIM1->CNT != 0U || TIM1->SR != 0U) { return fail(Stm32LcdDmaFault::Configuration); }
    context.flags = 0U; context.ended_valid = false; context.bytes = bytes; context.polls = 0U; context.started = DWT->CYCCNT;
    GPIOB->BSRR = context.words[0]; // Byte0 precedes CEN; first compare is +1us.
    __DSB();
    if ((GPIOB->ODR & DataMask) != (context.words[0] & DataMask)) { return fail(Stm32LcdDmaFault::Configuration); }
    context.state = TransferState::Pending;
    SET_BIT(DMA1_Channel5->CCR,DMA_CCR_EN);
    SET_BIT(DMA1_Channel2->CCR,DMA_CCR_EN);
    SET_BIT(DMA1_Channel3->CCR,DMA_CCR_EN);
    TIM1->DIER = Requests;
    __DMB();
    if (TIM1->DIER != Requests ||
        (DMA1_Channel5->CCR & DMA_CCR_EN) == 0U || (DMA1_Channel2->CCR & DMA_CCR_EN) == 0U ||
        (DMA1_Channel3->CCR & DMA_CCR_EN) == 0U) { return fail(Stm32LcdDmaFault::Configuration); }
    SET_BIT(TIM1->CR1,TIM_CR1_CEN);
    __DSB();
    if ((TIM1->CR1 & TIM_CR1_CEN) == 0U && !context.ended_valid) { return fail(Stm32LcdDmaFault::Configuration); }
    return true;
}

void stm32_lcd_dma_irq() noexcept
{
    if (!context.owned || context.state != TransferState::Pending) { return; }
    if (!clocks()) { context.fault = Stm32LcdDmaFault::Stop; dso_software_fault_entry(); }
    const auto flags = DMA1->ISR & (Errors | DMA_ISR_TCIF3);
    if (flags == 0U) { return; }
    context.flags |= flags;
    if ((flags & Errors) != 0U || ((flags & DMA_ISR_TCIF3) != 0U &&
        (DMA1_Channel2->CNDTR != 0U || DMA1_Channel3->CNDTR != 0U || DMA1_Channel5->CNDTR != 0U))) {
        context.fault = Stm32LcdDmaFault::Transfer;
    }
    if (!context.ended_valid) { context.ended = DWT->CYCCNT; context.ended_valid = true; }
    stop_engines(); // Clears level-sensitive IRQ sources before foreground poll.
    __DMB();
}

TransferState stm32_lcd_dma_poll() noexcept
{
    if (context.state != TransferState::Pending) { return context.state; }
    if (!stm32_foreground() || !clocks()) { static_cast<void>(fail(Stm32LcdDmaFault::Execution)); return context.state; }
    stm32_lcd_dma_irq(); // Also permits bounded completion polling without IRQ.
    ++context.polls;
    const auto elapsed = static_cast<std::uint32_t>(DWT->CYCCNT-context.started);
    if ((context.flags & Errors) != 0U) { static_cast<void>(fail(Stm32LcdDmaFault::Transfer)); }
    else if (context.ended_valid) {
        const auto hold = static_cast<std::uint32_t>(DWT->CYCCNT-context.ended);
        if (hold >= HalfRange || DMA1_Channel2->CNDTR != 0U || DMA1_Channel3->CNDTR != 0U || DMA1_Channel5->CNDTR != 0U ||
            !pins() || (GPIOC->ODR & (Select | Data | Write)) != (Data | Write) || (GPIOB->ODR & ReadReset) != ReadReset) {
            static_cast<void>(fail(Stm32LcdDmaFault::Transfer));
        } else if ((GPIOB->ODR & DataMask) != (context.words[context.bytes-1U] & DataMask)) {
            static_cast<void>(fail(Stm32LcdDmaFault::Transfer));
        } else if (hold >= HoldCycles) { context.state = TransferState::Complete; }
    } else if (elapsed >= TimeoutCycles || elapsed >= HalfRange) { static_cast<void>(fail(Stm32LcdDmaFault::Timing)); }
    if (context.state == TransferState::Pending && context.polls >= 4096U) { static_cast<void>(fail(Stm32LcdDmaFault::Timing)); }
    return context.state;
}

void stm32_lcd_dma_abort() noexcept
{
    if (!context.owned) { return; }
    stop_engines();
    clear_pending();
    context.ended_valid = false; context.flags = 0U;
    context.state = context.fault == Stm32LcdDmaFault::None ? TransferState::Idle : TransferState::Fault;
}

Stm32LcdDmaFault stm32_lcd_dma_fault() noexcept { return context.fault; }
} // namespace dso

extern "C" void dso_app_lcd_dma_irq(void) { dso::stm32_lcd_dma_irq(); }
