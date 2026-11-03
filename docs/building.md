# Building

## Toolchain

The CubeIDE project is configured for STM32CubeIDE **2.2.0**, GNU Tools for STM32 **14.3.rel1**, and an STM32CubeMX **6.18.1** hardware configuration. HAL/CMSIS sources are included.

## Build from the existing project

1. Configure the UID words and matching nonzero 64-bit identity in `firmware/App/Inc/dso_board_bringup.hpp` for the target MCU; see [Getting started](getting-started.md).
2. Open STM32CubeIDE and import `firmware/` as an existing project.
3. Select **Release** and choose **Project → Build Project**.
4. The ELF output is `firmware/Release/dso138.elf`.

The `.ioc` file is maintained as the hardware design reference, not the ordinary source of a regenerate-and-build workflow. Regenerating it would introduce the typical generated C entry, MSP and interrupt sources, which conflict with this project's independently authored C++ startup and peripheral owners unless manually integrated. Build the current CubeIDE project directly; do not casually regenerate the `.ioc`.

## Before programming

A successful compile does not establish suitability for every DSO138 or calibrate its analog front end. Confirm the board/display and UID configuration. Before programming, use STM32CubeProgrammer over SWD to save a full 64 KiB Flash backup and note the option-byte settings. See [Getting started](getting-started.md#4-build-back-up-and-program).

Program the ELF with CubeProgrammer and compare by readback. Avoid routine full-chip erase; when storage is enabled, the final 1 KiB page (`0x0800FC00–0x0800FFFF`) contains settings and calibration. Each MCU requires its own source configuration and build. The public UID-unconfigured source has not been newly flashed as a public image on hardware. USB remains disabled in the ordinary profile. An explicitly selected `DSO_USB_STOCK_BENCH_TRIAL` build opts into the factory-standard pull-up bench experiment described in [USB hardware and driver contract](usb-hardware.md); it is not a production-qualified wiring configuration.

## Publication checks

Release 1.0.1 builds in both Release and Debug with the listed toolchain. A representative nonzero identity build occupies 61,280 of the 61,440 application Flash bytes and reserves 16,368 of 20,480 SRAM bytes, including the 2 KiB stack; heap use is zero. The separately configured reference-device build has the same 61,280-byte load span in both configurations (61,276 section bytes plus four alignment bytes). The default zero-UID build occupies a 56,504-byte load span because activation paths become unreachable; use a configured build when assessing space. Different device constants, tools or configuration can change these figures.

Historical pre-removal software checks passed 46 LED/GPIO/Capture cases, a rolling acquisition suite with trigger handoff cases, and three fresh-process application startup/fault scenarios. The rolling acquisition path has since been removed and those rolling-suite results do not verify the current feature set. Eight ARM-emulated terminal-fault cases checked PA15 shutdown with its clock enabled or disabled on the configured Release ELF. These checks do not establish physical timing or a new hardware qualification. Revision 1.0.1 has not been flashed or tested on a physical board.

## Size optimization and future headroom

Historical measurement (before the 1 KiB storage profile and USB backend work): the configured optimized build occupied 59,440 bytes within a 63,488-byte application region, leaving 4,048 bytes. That layout reserved the final 2,048 bytes for storage. RAM comprised 112 bytes of initialized data, 14,468 bytes of BSS and 2,052 bytes of stack reservation/alignment. These measurements do not describe the current layout or qualify hardware. Current trial-build measurements are listed below.

Historical pre-USB verification: STM32CubeIDE headless imports built Release and Debug successfully and discovered the then-current 39 C++ sources, including FFT/spectrum and excluding rolling/filter modules. The existing non-LTO Capture override was retained. That image's callback-expanded stack audit bounded foreground plus conservative interrupt nesting at 1,544 bytes against the 2,048-byte minimum stack, leaving 504 bytes; the four alignment bytes were not counted as usable stack. These build and stack results do not cover the USB extension. See [Spectrum engineering](spectrum-design.md) for verification scope.

Both configurations retain `-Os`, LTO with one partition, section garbage collection and the existing non-LTO Capture translation unit. They now enable `-fipa-pta` for more precise interprocedural pointer analysis; startup and terminal application fault handlers have cold-path hints. No ABI or floating-point semantics were changed.

Controlled comparisons found no saving from `-Oz`, while `-O2` and the tested global no-inline settings were larger. On the same pending-event implementation before the cold-path hints, `-fipa-pta` reduced the load image from 61,520 to 61,304 bytes. The hints then reduced it to 61,280 bytes. A lossless packed-font prototype saved table bytes but increased total code size and was discarded. These are build-size observations, not speed measurements.

The 1.0.1 figures above are historical. The current storage profile uses one 1 KiB page at `0x0800FC00–0x0800FFFF`, leaving a 63 KiB application region. It stores settings and calibration only, does not migrate old data, and retains CRC validation. The page is not automatically erased; an interrupted explicit save can lose the prior stored data.

### Current USB stock-bench software measurement

The current R5 opt-in stock-board trial image occupies 64,456 bytes of its 64,512-byte application region, leaving 56 bytes. Its SRAM accounting is 112 bytes of `.data`, 13,192 bytes of `.bss`, and a 2,048-byte stack reservation, leaving 5,128 bytes unallocated; heap use is zero. Windows CDC enumeration and three automatic restart checks passed on the reference board. The R4 host tests covered held-frame transfers, host-requested Single captures, cancellation and timeout. These results do not establish electrical compliance, gap-free acquisition, or behavior on other board identities/toolchains. Source-default VID/PID are zero, and private test identifiers are not release values; further hardware results are in [USB test status](usb-hardware.md).

GNU make may report missing LTO `.su`/`.cyclo` sidecar targets; a linker warning about an executable writable SRAM load segment reflects the retained Flash-programming kernel. The application compiler treats source warnings as errors. For command-line builds, use the headless builder's `-no-indexer` option to avoid unrelated CDT indexer diagnostics.

The PDF manual illustrates the reference firmware interface. Use this repository's getting-started guide for the public source's per-board configuration and programming procedure.

The current one-page format can lose previously saved settings or calibration if reset or power fails during an explicit save. The old layout is not migrated.
