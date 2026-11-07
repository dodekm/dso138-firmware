# Hardware compatibility

## Checked configuration

Reference-board operation has been checked on JYE Tech DSO138 PCB **109-13800-00C**, using an STM32F103C8-class MCU with 64 KiB Flash and an **ILI9341** LCD controller. This is the only hardware combination represented by the public configuration. The public source has not itself been flashed as a UID-configured image on a physical instrument; compile or source-level status is not a claim of new board qualification.

The source begins with zero UID words and zero board identity. Before normal startup, configure these for your MCU as explained in [Getting started](getting-started.md). Verify device ID `0x410` with the programmer; firmware checks the configured full UID, 64 KiB Flash size and ILI9341 controller profile at activation. ES096 makes the debug ID register unreliable to running software.

Studio 2 connects to USB CDC firmware implementing waveform protocol v1.
The companion firmware adds remote settings and live successive captures.
R6 retains held downloads, Single and Repeat in Studio. See the [USB
hardware guide](usb-hardware.md) for the opt-in board configuration.

## Variants and limits

Other PCB revisions, display controllers, MCU variants and DSO138-family derivatives are not verified. Compatibility cannot be inferred from a similar connector or board name. This repository contains no DLO138 implementation or JYE Tech binary firmware.

The ADC's configured rate (up to 500 kS/s) does not guarantee analog bandwidth, voltage accuracy or timing performance. Component tolerances, front-end condition, probe setup and board-specific calibration affect readings. The project selects nominal gain 2 with board-specific zero code 133111936 (Q16 ADC code). These are configuration defaults, not a transferable calibration. This is not a claim of laboratory calibration or complete production qualification.

## Board-specific analog settings

Gain and zero parameters may differ between physical boards. The nominal
resolvers assume either the R11 = 1.5 kΩ, gain-2 front end or the R11 = 150 Ω,
gain-8 front end; firmware does not detect the fitted resistor.
`DSO_BOARD_NOMINAL_GAIN` is a compile-time topology selection, not a measurement.
Confirm the board topology before selecting it. If adjusting zero or other
transfer values, measure that instrument against a suitable reference and
record the method and previous values. Do not change the device identity or
hardware guards to make an unchecked variant boot. Preserve the final 1 KiB
settings/calibration page during programming if those values must be retained.
