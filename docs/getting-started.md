# Getting started

This guide is for DSO138 owners who want to build and program the firmware.

## Upgrade notes

These notes describe the pending v2.0.0 candidate in this source checkout. The latest published release is still v1.0.1; the candidate has not been released.

The core scope functions remain: ADC/DMA capture, Auto/Normal/Single triggering, Run/Hold, physical controls, D3 trigger indication, frequency, absolute peak (PK), peak-to-peak, mean and total RMS. The candidate also adds the experimental Spectrum view and opt-in USB waveform export; neither changes the ordinary USB-off source profile. Target FFT timing and production USB electrical compliance are not established.

Compared with the published v1.0.1 source, the candidate removes waveform save/recall, the slow 10–500 s/div rolling/filter timebases, minimum/maximum voltage readouts and AC RMS. It retains 18 triggered timebases from 10 µs/div through 5 s/div, together with absolute peak, peak-to-peak, mean and total RMS. FFT clipping warnings were removed during development of the new Spectrum view. Other experimental-only development behavior should not be assumed to carry forward.

The candidate reserves the final 1 KiB Flash page (`0x0800FC00–0x0800FFFF`) for current settings and DC calibration, and provides 63 KiB for the application. It does not convert records from older layouts, and the larger application region may overwrite old saved data during programming. Treat old records as disposable unless you retrieve needed data using the old firmware before reprogramming. The new firmware does not erase the page at boot. After upgrading, explicitly save settings and calibration in the new format. Saving does not make nominal calibration measured or more accurate.

Build from the public source after configuring your own MCU UID and board identity. USB remains disabled by default. The opt-in stock-board USB profile is a bench experiment, not a production-qualified configuration; no universal or identity-bound bench binary is supplied.

## 1. Configure the identity gate

The public source intentionally starts with three zero UID words and a zero board identity. Until you replace them with your own MCU's values, startup stops at the identity gate before display initialization. The blank display at this point is expected; it does not indicate an LCD fault.

## 2. Check hardware and tools

Use JYE Tech PCB **109-13800-00C**, an STM32F103C8-class device with 64 KiB Flash, and an **ILI9341** display. This is the only checked combination; see [Compatibility](compatibility.md). Required tools are STM32CubeIDE 2.2.0 (GNU Tools for STM32 14.3.rel1), an ST-Link or compatible SWD programmer, and STM32CubeProgrammer.

Power the instrument from its normal supply (nominally 9 V) with the usual ground connections. The ST-Link's 3.3 V target reference alone does not power the analog front end.

## 3. Set your own board identity

Read the MCU's 96-bit unique device ID with STM32CubeProgrammer's device information view. It consists of three 32-bit words, `word0`, `word1`, and `word2`, in the order displayed. In `firmware/App/Inc/dso_board_bringup.hpp`, replace the values in `BringupUid` with those words as hexadecimal `0x........U` constants. Set `BringupBoardIdentity` to the nonzero 64-bit value `(word2 << 32) | word0`, written `0xWWWWWWWWXXXXXXXXULL` (word2 first, then word0).

Use the exact values from your MCU. Do not remove or bypass the identity, MCU-type, Flash-size or display-controller guards. The firmware expects device ID `0x410`, 64 KiB Flash and ILI9341 (`0x9341`); mismatches must prevent activation. This is per-device configuration, not universal identity provisioning.

## 4. Build and program

1. Follow [Building](building.md) to build the Release ELF for your configured UID.
2. Connect SWDIO, SWCLK, GND and target reference voltage. Keep the board on its normal supply; the programmer's 3.3 V output is not analog-board power.
3. In STM32CubeProgrammer, connect over **SWD** and verify the detected MCU type and 64 KiB Flash capacity. Do not change option bytes.
4. Program `firmware/Release/dso138.elf` through CubeProgrammer and perform a readback comparison.
5. Preserve the final 1 KiB Flash page (`0x0800FC00–0x0800FFFF`) to keep saved settings or calibration. Avoid full-chip erase; it erases the page as well as the application.
6. Disconnect SWD and power the instrument normally. Check startup, display and controls before connecting a signal.

Build separately for each MCU; an image made with your UID is not a universal image. The [user manual (PDF)](manual/dso138-user-manual.pdf) and [editable LaTeX source](manual/dso138-user-manual.tex) cover the controls and operation.

Storage retains settings and DC calibration only; waveform save/recall and migration of the older layout are removed. The page is not erased automatically at boot. An interrupted explicit save can lose the prior saved data. USB is disabled in the ordinary source profile. The opt-in stock-board experiment is not electrically production-qualified; see [USB hardware limits](usb-hardware.md).
