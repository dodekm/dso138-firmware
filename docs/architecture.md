# Architecture overview

## Runtime shape

The firmware uses C++17, static allocation and a foreground event loop. It has no RTOS and the application does not use a heap. The independently authored platform files provide startup, MCU peripheral setup and interrupt routing; the application core then initializes the display, panel, acquisition and user interface according to the selected board profile.

`dso_platform_startup.cpp` owns the C++ entry path and startup sequencing. `dso_platform_msp.cpp` owns HAL peripheral pin and DMA setup. `dso_platform_interrupts.cpp` routes interrupt handlers. `dso_app.cpp` coordinates the foreground lifecycle and services the `dso_ui_controller.cpp` user interface.

## Acquisition and presentation

`dso_stm32_acquisition.cpp` configures ADC1, TIM2 and DMA1 Channel 1 for waveform sampling. Capture planning and trigger behavior live in `dso_capture.cpp` and `dso_acquisition.cpp`; the DMA ring and record ownership are handled by `dso_dma_ring.cpp`. Completed records are published as immutable captures for UI processing. Held captures preserve their data while settings or later acquisitions change.

`dso_ui_controller.cpp` connects front-panel state, trigger/capture state, measurements and rendering. The current timebase catalogue has 18 triggered settings from 10 µs/div through 5 s/div; it does not include the former slow rolling and filtered acquisition path. LCD updates use the ILI9341 display path; `dso_stm32_lcd_dma.cpp` owns the bounded DMA transport used by the display renderer.

## Peripheral ownership

| Resource | Role |
| --- | --- |
| ADC1, TIM2, DMA1 Channel 1 | Waveform sample acquisition |
| ADC2, PA1–PA3 | Front-panel analog switches and controls |
| TIM1, DMA1 Channels 2, 3 and 5 | LCD write transport |
| TIM3, PA7 | Optional test output |
| TIM4, PB8/PB9 | Analog power/reference control |
| PA15 (JTDI), D3 | Active-low accepted-trigger activity LED |
| SWD | Retained for programming and debugging |

`dso_stm32_shared_guard.cpp` and the relevant peripheral adapters enforce resource preconditions and ownership boundaries. Optional test output and storage are separate profile choices, not required for the user interface.

The trigger activity LED is initialized only after the board passes its UID-guarded activation. The platform has already released JTAG while retaining SWD; PA15 is then configured as a 2 MHz push-pull output after preloading the inactive HIGH level and changing only its GPIO configuration field. D3 is wired through R28 (470 Ω) to +3.3 V, so driving PA15 LOW lights it. A single foreground, nonblocking 100 ms pulse is restarted by accepted trigger events. Each acquisition engine owns a pending-event flag, preserved through rearm until the application consumes both flags; no interrupt accesses this state. Multiple unseen edges coalesce into one indication. It does not depend on display refresh. See [Trigger LED behavior](trigger-led.md) for mode details and limits.

The LED indicates accepted software trigger events, not an electrical trigger strobe or timing measurement. Accepted edges can be indicated in eligible Auto, Normal, and Single captures. Auto free-run timeouts, HOLD redraws, and crossings rejected before sufficient pre-trigger history do not create an LED pulse. The pulse remains active through Single's automatic HOLD and is forced inactive on startup/application faults; the HardFault/NMI entry path also drives PA15 inactive. Closely spaced accepted edges can make the LED appear continuously lit. This software behavior has not yet been verified on flashed hardware.

## Memory and storage

The current storage linker layout provides 63 KiB for the application and reserves one 1 KiB Flash page (0x0800FC00–0x0800FFFF). The target has 20 KiB SRAM; the linker reserves a 2 KiB stack region. The storage layer and board-specific Flash adapter are in `dso_storage.cpp` and `dso_stm32_storage.cpp`.

The storage service retains only current settings and nine-slot DC calibration; waveform persistence is removed. The page is not erased automatically at boot. Explicit Save Config or Save Cal operations erase/program the page; reset or power failure during that operation can lose the prior saved data. CRC, readback and final commit validation reject incomplete records. The prior layout is not migrated, and its old data is no longer preserved. The page has a 32-byte envelope, a 36-byte settings record and up to 204 bytes of calibration data (272 bytes total), leaving 752 bytes unused.

Legacy storage schemas are not supported or migrated. Current records retain CRC validation. Acquisition exports raw ADC samples; obsolete filtering metadata and its presentation paths have been removed.

## Status and limits

Firmware readiness and capture publication status describe software state. They do not prove continuous external sample integrity or characterize analog accuracy. The configured sample rate is not a bandwidth specification. The default nominal gain and zero values are board-specific starting parameters, not a calibration that transfers to another instrument.

The public configuration begins with zero UID words and zero board identity. USB remains off in the ordinary profile. An explicit `DSO_USB_STOCK_BENCH_TRIAL` build enables a bench-only factory-standard pull-up experiment; it does not use PA4/PA9 or implement the recommended switched 3.0–3.6 V pull-up/VBUS-sense production wiring. The configured R5 trial image is 64,456 bytes in a 64,512-byte application region (56 bytes free); SRAM use is 112 bytes `.data`, 13,192 bytes `.bss`, and a reserved 2,048-byte stack, with 5,128 bytes unallocated and no heap. Windows CDC enumeration and three automatic restart checks passed on the reference board. R4 host tests covered held-frame downloads, host-requested Single captures, cancellation and timeout. These checks do not establish electrical compliance or gap-free acquisition. Default VID/PID are zero, and private test identifiers are not release values. Each owner must set their MCU UID and corresponding nonzero identity in `dso_board_bringup.hpp`; device, Flash-size and display-controller checks remain in place. See [Getting started](getting-started.md), [Compatibility](compatibility.md) and [USB hardware and driver contract](usb-hardware.md) before programming hardware.
