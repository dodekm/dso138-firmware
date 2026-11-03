# Building

For the Windows application, see [Studio build and package instructions](../host/windows/README.md#build-and-package). The current board-specific firmware measurements are in [Configured Studio 2 firmware](#configured-studio-2-firmware).

## Toolchain

The CubeIDE project is configured for STM32CubeIDE **2.2.0**, GNU Tools for STM32 **14.3.rel1**, and an STM32CubeMX **6.18.1** hardware configuration. HAL/CMSIS sources are included.

## Build from the existing project

1. Configure the UID words and matching nonzero 64-bit identity in `firmware/App/Inc/dso_board_bringup.hpp` for the target MCU; see [Getting started](getting-started.md).
2. Open STM32CubeIDE and import `firmware/` as an existing project.
3. Select **Release** and choose **Project → Build Project**.
4. The ELF output is `firmware/Release/dso138.elf`.

The checked-in `firmware/dso138.ioc` is a partial board-configuration reference, not a complete generator input. It records the STM32F103C8Tx, main clocks, ADC1/ADC2 channels, TIM2, ADC1 circular DMA, NVIC priorities, buttons, PA15 LED and SWD. Its peripheral list does not model the LCD DMA/timer transport or the USB device driver; the 48 MHz USB clock value is present without a USB IP configuration. The project instead has a hand-authored C++ `main` in `dso_platform_startup.cpp`, a checked-in startup assembly file, and custom MSP and interrupt owners. The `.ioc` also says `NoMain=false` and `DeletePrevious=true`. No safe CubeMX regeneration-and-merge workflow has been validated, and generated C entry/MSP/IRQ output could conflict with or replace project-owned integration. Import and build the existing `firmware/` CubeIDE project as supplied; do not generate code from this `.ioc` as part of ordinary build or recovery.

## Before programming

A successful compile does not establish suitability for every DSO138 or calibrate its analog front end. Confirm the board/display and UID configuration. Before programming, verify the exact MCU and Flash capacity in STM32CubeProgrammer. Do not change option bytes or use a full-chip erase as a routine recovery step. See [Getting started](getting-started.md#4-build-and-program) and [release readiness](release-readiness.md).

Program the ELF with CubeProgrammer and compare by readback. Avoid routine full-chip erase; when storage is enabled, the final 1 KiB page (`0x0800FC00–0x0800FFFF`) contains settings and calibration. Each MCU requires its own source configuration and build. The public UID-unconfigured source has not been newly flashed as a public image on hardware. USB remains disabled in the ordinary profile. An explicitly selected `DSO_USB_STOCK_BENCH_TRIAL` build opts into the factory-standard pull-up bench experiment described in [USB hardware and driver contract](usb-hardware.md); it is not a production-qualified wiring configuration.

## Publication checks

Release 1.0.1 builds in both Release and Debug with the listed toolchain. A representative nonzero identity build occupies 61,280 of the 61,440 application Flash bytes and reserves 16,368 of 20,480 SRAM bytes, including the 2 KiB stack; heap use is zero. The separately configured reference-device build has the same 61,280-byte load span in both configurations (61,276 section bytes plus four alignment bytes). The default zero-UID build occupies a 56,504-byte load span because activation paths become unreachable; use a configured build when assessing space. Different device constants, tools or configuration can change these figures.

Historical pre-removal software checks passed 46 LED/GPIO/Capture cases, a rolling acquisition suite with trigger handoff cases, and three fresh-process application startup/fault scenarios. The rolling acquisition path has since been removed and those rolling-suite results do not verify the current feature set. Eight ARM-emulated terminal-fault cases checked PA15 shutdown with its clock enabled or disabled on the configured Release ELF. These checks do not establish physical timing or a new hardware qualification. At the time of those checks, release 1.0.1 had not been flashed or tested on a physical board; subsequent R5 hardware observations are listed below.

## Size optimization and future headroom

Historical measurement (before the 1 KiB storage profile and USB backend work): the configured optimized build occupied 59,440 bytes within a 63,488-byte application region, leaving 4,048 bytes. That layout reserved the final 2,048 bytes for storage. RAM comprised 112 bytes of initialized data, 14,468 bytes of BSS and 2,052 bytes of stack reservation/alignment. These measurements do not describe the current layout or qualify hardware. Current trial-build measurements are listed below.

Historical pre-USB verification: STM32CubeIDE headless imports built Release and Debug successfully and discovered the then-current 39 C++ sources, including FFT/spectrum and excluding rolling/filter modules. The existing non-LTO Capture override was retained. That image's callback-expanded stack audit bounded foreground plus conservative interrupt nesting at 1,544 bytes against the 2,048-byte minimum stack, leaving 504 bytes; the four alignment bytes were not counted as usable stack. These build and stack results do not cover the USB extension. See [Spectrum engineering](spectrum-design.md) for verification scope.

The R6 reference used `-Os`, one LTO partition, section garbage
collection, and `-fipa-pta`. The Studio 2 project keeps those optimization
passes and the existing non-LTO Capture translation unit. Its Debug and
Release linker options use balanced LTO partitions and sort sections by
alignment to reduce the USB-enabled image's load span.

Controlled comparisons found no saving from `-Oz`, while `-O2` and the tested global no-inline settings were larger. On the same pending-event implementation before the cold-path hints, `-fipa-pta` reduced the load image from 61,520 to 61,304 bytes. The hints then reduced it to 61,280 bytes. A lossless packed-font prototype saved table bytes but increased total code size and was discarded. These are build-size observations, not speed measurements.

The 1.0.1 figures above are historical. The current storage profile uses one 1 KiB page at `0x0800FC00–0x0800FFFF`, leaving a 63 KiB application region. It stores settings and calibration only, does not migrate old data, and retains CRC validation. The page is not automatically erased; an interrupted explicit save can lose the prior stored data.

### Configured R6 USB bench build

Clean headless CubeIDE Release and Debug builds produced identical 64,408-byte application images with USB and the reference board's features enabled. The loadable span ends at `0x0800FB98`, leaving 104 bytes before the storage boundary at `0x0800FC00`. CubeProgrammer verified the installed image after programming pages 0–62. Independent SWD comparison matched the application bytes in memory without creating a backup file.

R6 saves 48 bytes from R5 by discarding `.fini_array` and `.dtors`. It retains `.init_array`, scope-local cleanup and explicit stop/fault cleanup. SRAM accounting is 112 bytes of `.data`, 13,192 bytes of `.bss`, and 2,048 bytes of reserved stack, leaving 5,128 bytes unallocated. Heap use is zero. The static model is 1,508 bytes and the engineering budget is 1,544 bytes. The observed stack write watermark reached 928 bytes during bounded board runs. The analysis and bench procedures are in [release verification](release-readiness.md).

This bare-metal program never returns from `main` or calls `exit`. It has no process-exit cleanup contract: code must not depend on global/static destructors running at shutdown. Startup initialization through `.init_array`, ordinary local RAII destructors at scope exit, and explicit stop/fault cleanup remain part of the design.

The verified CubeIDE run emitted 117 auxiliary `.su`/`.cyclo` make sidecar-target warnings and a CDT scanner warning because host `g++` was not installed; the ARM cross-compiler and linker completed successfully. The linker may also warn about the executable writable SRAM load segment used by the retained Flash-programming kernel. The application compiler treats source warnings as errors. For command-line builds, use the headless builder's `-no-indexer` option to avoid unrelated CDT indexer diagnostics.

The PDF manual illustrates the reference firmware interface. Use this repository's getting-started guide for the public source's per-board configuration and programming procedure.

The current one-page format can lose previously saved settings or calibration if reset or power fails during an explicit save. The old layout is not migrated.

### Configured Studio 2 firmware

A clean CubeIDE 2.2.0 import with the same private board identity and
USB bench options links to **64,496 bytes of application Flash**,
leaving **16 bytes** before the storage page. Debug and Release
produced byte-identical binaries. SRAM uses
112 bytes of initialized data and
13,136 bytes of BSS. The stack reservation stays at 2,048 bytes, leaving
5,184 bytes of unallocated SRAM. The linker keeps the heap at zero.

The current firmware borrows the held waveform under the controller's
existing token, writes replies directly into the retry cache, and
shares the foreground admission check. These changes make room for the
live settings and acquisition commands. The configured build size
includes those commands. See the [Studio 2 engineering plan](studio-2-design.md)
for verification scope.
