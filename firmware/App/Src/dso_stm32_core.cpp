#include "main.h"
#include "dso_app.h"

#include <cstdint>

extern "C" std::uint32_t dso_app_prepare_core(void)
{
    const std::uint32_t control = SCB->CCR;
    SCB->CCR = control | SCB_CCR_STKALIGN_Msk;
    __DSB();
    __ISB();
    return (SCB->CCR & SCB_CCR_STKALIGN_Msk) != 0U ? 1U : 0U;
}
