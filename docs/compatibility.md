# Hardware compatibility

## Checked configuration

Reference-board operation has been checked on JYE Tech DSO138 PCB **109-13800-00C**, using an STM32F103C8-class MCU with 64 KiB Flash and an **ILI9341** LCD controller. This is the only hardware combination represented by the public configuration. The public source has not itself been flashed as a UID-configured image on a physical instrument; compile or source-level status is not a claim of new board qualification.

The source begins with zero UID words and zero board identity. Before normal startup, configure these for your MCU as explained in [Getting started](getting-started.md). The device ID `0x410`, 64 KiB Flash-size and ILI9341 controller checks are required; keep them enabled.

## Variants and limits

Other PCB revisions, display controllers, MCU variants and DSO138-family derivatives are not verified. Compatibility cannot be inferred from a similar connector or board name. This repository contains no DLO138 implementation or JYE Tech binary firmware.

The ADC's configured rate (up to 500 kS/s) does not guarantee analog bandwidth, voltage accuracy or timing performance. Component tolerances, front-end condition, probe setup and board-specific calibration affect readings. The project selects nominal gain 2 with board-specific zero code 133111936 (Q16 ADC code). These are configuration defaults, not a transferable calibration. This is not a claim of laboratory calibration or complete production qualification.

## Board-specific analog settings

Gain and zero parameters may differ between physical boards. If adjusting them, measure the specific instrument against a suitable reference and record the method and prior calibration values. Do not change the device identity or hardware guards to make an unchecked variant boot. Preserve the final 1 KiB settings/calibration page during programming if those values must be retained.
