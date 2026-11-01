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

Program the ELF with CubeProgrammer and compare by readback. Avoid routine full-chip erase; the final 4 KiB contains the persistent journal. Each MCU requires its own source configuration and build. The public UID-unconfigured source has not been newly flashed as a public image on hardware.

## Publication checks

The configured public C++ source builds in both Release and Debug with the listed toolchain. A representative nonzero identity build occupies 61,256 of the 61,440 application Flash bytes and reserves 16,352 of 20,480 SRAM bytes, including the 2 KiB stack; heap use is zero. Different tools or configuration can change these figures. Fifteen private host comparisons matched the reference initialization, error paths and IRQ dispatch. These checks do not establish physical timing or a new hardware qualification.

GNU make may report missing LTO `.su`/`.cyclo` sidecar targets; a linker warning about an executable writable SRAM load segment reflects the retained Flash-programming kernel. The application compiler treats source warnings as errors. For command-line builds, use the headless builder's `-no-indexer` option to avoid unrelated CDT indexer diagnostics.

The PDF manual illustrates the reference firmware interface. Use this repository's getting-started guide for the public source's per-board configuration and programming procedure.
