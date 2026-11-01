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
| SWD | Retained for programming and debugging |

`dso_stm32_shared_guard.cpp` and the relevant peripheral adapters enforce resource preconditions and ownership boundaries. Optional test output and storage are separate profile choices, not required for the user interface.

## Memory and storage

The linker layout provides 60 KiB for the application and reserves the final 4 KiB of Flash for the persistent journal. The target has 20 KiB SRAM; the linker reserves a 2 KiB stack region. The storage layer and board-specific Flash adapter are in `dso_storage.cpp` and `dso_stm32_storage.cpp`.

## Status and limits

Firmware readiness and capture publication status describe software state. They do not prove continuous external sample integrity or characterize analog accuracy. The configured sample rate is not a bandwidth specification. The default nominal gain and zero values are board-specific starting parameters, not a calibration that transfers to another instrument.

The public configuration begins with zero UID words and zero board identity. Each owner must set their MCU UID and the corresponding nonzero identity in `dso_board_bringup.hpp`; device, Flash-size and display-controller checks remain in place. See [Getting started](getting-started.md) and [Compatibility](compatibility.md) before programming hardware.
