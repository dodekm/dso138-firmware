#include "dso_board_bringup.hpp"
#if DSO_BOARD_BRINGUP
#include "dso_display.hpp"
#include "dso_stm32_analog_power.hpp"
#include "dso_stm32_display.hpp"
#include "dso_stm32_acquisition.hpp"
#if DSO_BOARD_TEST_OUTPUT
#include "dso_stm32_test_output.hpp"
#endif
#include "dso_stm32_panel.hpp"
#include "main.h"

namespace dso {
volatile BringupStatus dso_bringup_status{};

bool activate_bringup_board(Ili9341Display& display) noexcept
{
    dso_bringup_status.stage = 1U;
    const auto debug_id = DBGMCU->IDCODE;
    // ES096: DBGMCU can read zero outside a debug session. The complete UID
    // and specified Flash-size checks remain mandatory for standalone boot.
    if ((debug_id != 0U && (debug_id & 0xfffU) != 0x410U) ||
        *reinterpret_cast<volatile const std::uint16_t*>(FLASHSIZE_BASE) != 64U ||
        HAL_GetUIDw0() != BringupUid[0] || HAL_GetUIDw1() != BringupUid[1] || HAL_GetUIDw2() != BringupUid[2] ||
        BringupLcdId != 0x9341U) { return false; }
#if DSO_BOARD_TEST_OUTPUT
    // The reset/direct-Flash/TIM3 ownership and electrical qualification inputs
    // are explicit bench assumptions. This code does not measure or prove them.
    constexpr Stm32Tim3StartupContract startup_contract{true, true, true, true};
    constexpr Stm32TestOutputQualification test_output_qualification{true, true, true, true};
    constexpr Stm32TestOutputProfile test_output_profile{BringupBoardIdentity};
    dso_bringup_status.stage = 0x70U;
    if (!start_stm32_test_output_after_system_reset(test_output_profile, startup_contract,
                                                    test_output_qualification)) {
        return false;
    }
#endif
    // Enable the interface for the display owner's read-only backup checks;
    // do not reset the backup domain or alter LSE/tamper/RTC outputs.
    __HAL_RCC_BKP_CLK_ENABLE();
    dso_bringup_status.stage = 2U;
    const auto& power = stm32_analog_power_api();
    // Explicit operator-authorized classic-board bench assumptions. Digital
    // ownership/timing guards still apply; regulated rails are not inferred.
    constexpr Stm32AnalogPowerQualification power_permissions{true, true, true, true};
    constexpr Stm32AnalogPowerProfile profile{BringupBoardIdentity, 1023U, 2047U};
    if (!power.start(profile, power_permissions) ||
        power.state() != Stm32AnalogPowerState::CarrierRunning || !power.carrier_may_be_active()) {
        dso_bringup_status.power_fault = static_cast<std::uint32_t>(power.fault());
        return false;
    }
    dso_bringup_status.stage = 3U;
    if (!prepare_stm32_display({true, true, true, true}) ||
        stm32_display_state() != Stm32DisplayState::Prepared) {
        dso_bringup_status.display_fault = static_cast<std::uint32_t>(stm32_display_fault());
        return false;
    }
    dso_bringup_status.stage = 4U;
    if (!display.initialize(ControllerId::Ili9341, ColorOrder::Bgr, DisplayOrientation::Landscape180)) {
        dso_bringup_status.display_fault = 0x100U + static_cast<std::uint32_t>(display.status());
        return false;
    }
    if (!enable_stm32_display_dma()) {
        dso_bringup_status.display_fault = 0x200U;
        return false;
    }
    dso_bringup_status.lcd_id = BringupLcdId;
    dso_bringup_status.stage = 5U;
#if DSO_BOARD_ACQUISITION
    // Operator-authorized RAW bench operation, not metrological acceptance.
    // Idle PA0 and the current switch tuple were read through SWD beforehand.
    // Nominal source settling / analog rails still require instrumentation.
    if (!prepare_stm32_panel(true) || !qualify_stm32_acquisition({true, true})) { return false; }
    dso_bringup_status.stage = 6U;
#endif
    return true;
}

std::uint32_t bringup_cycles() noexcept { return DWT->CYCCNT; }

std::uint16_t bringup_buttons() noexcept
{
    return static_cast<std::uint16_t>(GPIOB->IDR);
}
} // namespace dso
#else
namespace dso {
// Retained verification entry in ordinary builds. Compile-time dormancy is
// unconditional and grants no peripheral authority, even when called directly.
bool activate_bringup_board(Ili9341Display&) noexcept { return false; }
} // namespace dso
#endif
