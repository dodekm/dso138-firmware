# Architecture overview

## Runtime shape

The firmware uses C++17, static allocation and a foreground event loop. It has no RTOS and the application does not use a heap. The independently authored platform files provide startup, MCU peripheral setup and interrupt routing; the application core then initializes the display, panel, acquisition and user interface according to the selected board profile.

`dso_platform_startup.cpp` owns the C++ entry path and startup sequencing. `dso_platform_msp.cpp` owns HAL peripheral pin and DMA setup. `dso_platform_interrupts.cpp` routes interrupt handlers. `dso_app.cpp` coordinates the foreground lifecycle and services the `dso_ui_controller.cpp` user interface.

## Acquisition and presentation

`dso_stm32_acquisition.cpp` configures ADC1, TIM2 and DMA1 Channel 1 for waveform sampling. Capture planning and trigger behavior live in `dso_capture.cpp` and `dso_acquisition.cpp`; the DMA ring and record ownership are handled by `dso_dma_ring.cpp`. Completed records are published as immutable captures for UI processing. Held captures preserve their data while settings or later acquisitions change.

`dso_ui_controller.cpp` connects front-panel state, trigger/capture state, measurements and rendering. Slow timebases use rolling acquisition and FIR decimation (`dso_rolling.cpp`, `dso_decimator.cpp`). LCD updates use the ILI9341 display path; `dso_stm32_lcd_dma.cpp` owns the bounded DMA transport used by the display renderer.

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

The LED indicates accepted software trigger events, not an electrical trigger strobe or timing measurement. Fast actual edges can be indicated in eligible Auto, Normal, and Single captures. Accepted Normal/Single edges on slow timebases can also light the LED before the processed post-trigger record is ready. Auto free-run timeouts, recall/HOLD redraws, slow Auto rolling updates, and crossings rejected before sufficient pre-trigger history do not create an LED pulse. The pulse remains active through Single's automatic HOLD and is forced inactive on startup/application faults; the HardFault/NMI entry path also drives PA15 inactive. Closely spaced accepted edges can make the LED appear continuously lit. This software behavior has not yet been verified on flashed hardware.

## Memory and storage

The linker layout provides 60 KiB for the application and reserves the final 4 KiB of Flash for the persistent journal. The target has 20 KiB SRAM; the linker reserves a 2 KiB stack region. The storage layer and board-specific Flash adapter are in `dso_storage.cpp` and `dso_stm32_storage.cpp`.

## Status and limits

Firmware readiness and capture publication status describe software state. They do not prove continuous external sample integrity or characterize analog accuracy. The configured sample rate is not a bandwidth specification. The default nominal gain and zero values are board-specific starting parameters, not a calibration that transfers to another instrument.

The public configuration begins with zero UID words and zero board identity. Each owner must set their MCU UID and the corresponding nonzero identity in `dso_board_bringup.hpp`; device, Flash-size and display-controller checks remain in place. See [Getting started](getting-started.md) and [Compatibility](compatibility.md) before programming hardware.
