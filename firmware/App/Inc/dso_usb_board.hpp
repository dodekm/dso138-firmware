#ifndef DSO_USB_BOARD_HPP
#define DSO_USB_BOARD_HPP

#include "dso_board_bringup.hpp"
#include "dso_stm32_usb.hpp"

#ifndef DSO_BOARD_USB
#define DSO_BOARD_USB 0
#endif
static_assert(DSO_BOARD_USB == 0 || DSO_BOARD_USB == 1, "Invalid USB profile");
static_assert(!DSO_BOARD_USB || (DSO_BOARD_BRINGUP && DSO_BOARD_ACQUISITION),
              "USB requires the identity-bound classic board profile");

namespace dso {
// Deliberately unqualified. Inspect/rework the USB pull-up, VBUS divider and
// default-off switch before changing this per-board profile. See usb-hardware.md.
// VID/PID also default to zero; configure identifiers authorized for your device.
inline constexpr Stm32UsbQualification UsbBoardQualification{};
} // namespace dso

#endif
