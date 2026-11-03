# Getting started

This guide is for DSO138 owners who want to build and program the firmware.

## 1. Configure the identity gate

The public source intentionally starts with three zero UID words and a zero board identity. Until you replace them with your own MCU's values, startup stops at the identity gate before display initialization. The blank display at this point is expected; it does not indicate an LCD fault.

## 2. Check hardware and tools

Use JYE Tech PCB **109-13800-00C**, an STM32F103C8-class device with 64 KiB Flash, and an **ILI9341** display. This is the only checked combination; see [Compatibility](compatibility.md). Required tools are STM32CubeIDE 2.2.0 (GNU Tools for STM32 14.3.rel1), an ST-Link or compatible SWD programmer, and STM32CubeProgrammer.

Power the instrument from its normal supply (nominally 9 V) with the usual ground connections. The ST-Link's 3.3 V target reference alone does not power the analog front end.

## 3. Set your own board identity

Read the MCU's 96-bit unique device ID with STM32CubeProgrammer's device information view. It consists of three 32-bit words, `word0`, `word1`, and `word2`, in the order displayed. In `firmware/App/Inc/dso_board_bringup.hpp`, replace the values in `BringupUid` with those words as hexadecimal `0x........U` constants. Set `BringupBoardIdentity` to the nonzero 64-bit value `(word2 << 32) | word0`, written `0xWWWWWWWWXXXXXXXXULL` (word2 first, then word0).

Use the exact values from your MCU. Do not remove or bypass the identity, MCU-type, Flash-size or display-controller guards. The firmware expects device ID `0x410`, 64 KiB Flash and ILI9341 (`0x9341`); mismatches must prevent activation. This is per-device configuration, not universal identity provisioning.

## 4. Build, back up and program

1. Follow [Building](building.md) to build the Release ELF for your configured UID.
2. Connect SWDIO, SWCLK, GND and target reference voltage. Keep the board on its normal supply; the programmer's 3.3 V output is not analog-board power.
3. In STM32CubeProgrammer, connect over **SWD**, save a backup of the complete 64 KiB Flash contents, and record option-byte settings before changing anything.
4. Program `firmware/Release/dso138.elf` through CubeProgrammer and perform a readback comparison.
5. Preserve the final 1 KiB Flash page (`0x0800FC00–0x0800FFFF`) if you want to keep saved settings or calibration. Avoid full-chip erase as a default; it erases the page as well as the application.
6. Disconnect SWD and power the instrument normally. Check startup, display and controls before connecting a signal.

Build separately for each MCU; an image made with your UID is not a universal image. The [user manual (PDF)](manual/dso138-user-manual.pdf) and [editable LaTeX source](manual/dso138-user-manual.tex) cover the controls and operation.

Storage retains settings and DC calibration only; waveform save/recall and migration of the older layout are removed. The page is not erased automatically at boot. An interrupted explicit save can lose the prior saved data. Public USB remains disabled and unqualified.
