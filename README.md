# DSO138 Oscilloscope Firmware

C++17 firmware for the JYE Tech DSO138 digital oscilloscope, built with STM32 HAL for the STM32F103C8T6 in STM32CubeIDE. This repository provides the application source, CubeIDE project, and practical documentation for owners and developers.

[User manual (PDF)](docs/manual/dso138-user-manual.pdf) · [Getting started](docs/getting-started.md) · [Build instructions](docs/building.md) · [Release readiness](docs/release-readiness.md) · [Compatibility](docs/compatibility.md) · [Trigger LED](docs/trigger-led.md)

Planning an update? Read the [v2 candidate upgrade notes](docs/getting-started.md#upgrade-notes) first. The latest published release remains v1.0.1; this source branch is still an unreleased candidate.

USB waveform export is an opt-in stock-board experiment. The ordinary build keeps it off. See [USB setup and test status](docs/usb-hardware.md), the [waveform protocol](docs/usb-protocol.md), and the [MATLAB client instructions](host/matlab/README.md).

![Illustrative DSO138 screen preview](docs/screen-preview.svg)

*Illustrative interface artwork, not a device screenshot or measured trace.*

> **Before flashing:** the reference hardware is PCB **109-13800-00C**, **STM32F103C8T6** and **ILI9341**. Configure your own MCU identity using [Getting started](docs/getting-started.md). Other variants are unverified, and nominal readings require individual calibration.

## Features

| Area | Included functionality |
| --- | --- |
| Acquisition | Timer-triggered ADC with DMA; configured rate up to 500 kS/s |
| Timebase | 18 settings from 10 µs/div to 5 s/div; triggered capture |
| Controls | Auto, Normal and Single trigger, Run/Hold, trigger position, physical switches and four buttons |
| Trigger indication | D3 flashes on accepted trigger crossings; see [Trigger LED behavior](docs/trigger-led.md) |
| Measurements | Frequency and absolute peak focus-line readings; peak-to-peak on Amplitude; total RMS and mean on RMS/Mean; raw ADC counts when voltage conversion is unavailable |
| Spectrum (experimental) | Software-verified 512-point real FFT, periodic Hann window, peak-referenced dBFS and dominant positive-frequency bin; target timing remains unverified |
| USB waveform export (opt-in experiment) | Verified CDC held-frame reads and host-requested Single captures on the reference board. See test results and bench limits above. |
| Storage (opt-in) | Save/load scope settings and save/restore DC calibration; waveform persistence is removed |

The current storage profile reserves one 1 KiB Flash page at `0x0800FC00–0x0800FFFF` and gives the application 63 KiB. It stores scope settings and DC calibration only; waveform save/recall and migration of the older layout are removed. The page is not erased automatically at boot. An interrupted explicit save can lose the prior saved settings or calibration. CRC validation remains enabled.

The compact storage record uses at most 272 bytes (32-byte journal envelope and up to 240 bytes of settings plus DC calibration). The deployed R5 USB trial occupies 64,456 bytes of its 64,512-byte application region, leaving 56 bytes; the ordinary public profile remains USB-off. A fully configured, clean-copy-verified but unflashed R6 USB-trial software image is 64,408 bytes (104 bytes free) and does not change the deployed R5 image. Host enumeration, held-frame transfers, host-requested Single captures, cancellation, timeout and three automatic restart checks were verified on the reference board. Electrical compliance and gap-free acquisition are not qualified. See [Build results](docs/building.md) and [Release readiness](docs/release-readiness.md) for software resource measurements. The spectrum view is software-verified; current target timing verification is pending. Use SEL to reach Domain, then + for Spectrum or - for Scope; OK keeps its normal RUN/HOLD behavior. See [Spectrum engineering and limitations](docs/spectrum-design.md).

## Quick start

1. Read [Getting started](docs/getting-started.md): identify the board, set its MCU UID and identity, and preserve the final settings/calibration Flash page when programming.
2. Follow [Building](docs/building.md) to import `firmware/` as an existing STM32CubeIDE project and build the Release configuration.
3. Review [Compatibility](docs/compatibility.md) before using another board revision. For common startup and signal issues, see [Troubleshooting](docs/troubleshooting.md).

The checked toolchain is STM32CubeIDE 2.2.0 with GNU Tools for STM32 14.3.rel1; the CubeMX configuration was generated with 6.18.1. Do not casually regenerate the `.ioc` file. It is maintained as the hardware design reference; regeneration would add the usual generated C entry/startup support and requires manual integration with this project's C++ startup and interrupt owners.

The image is identity-bound and must be built for each MCU. The default source keeps USB disabled; the stock-bench profile is opt-in and is not an electrically qualified production configuration. No universal prebuilt image is provided. This source does not include JYE Tech binary firmware or DLO138 implementation code.

## Documentation

- [User manual (PDF)](docs/manual/dso138-user-manual.pdf) · [LaTeX source](docs/manual/dso138-user-manual.tex)
- [Architecture overview](docs/architecture.md)
- [Contributing](CONTRIBUTING.md)
- [Changelog](CHANGELOG.md)

## License

The application and documentation are licensed under the [MIT License](LICENSE). Third-party components retain their own terms; see [third-party notices](THIRD_PARTY_NOTICES.md).
