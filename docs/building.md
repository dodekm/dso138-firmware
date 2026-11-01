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

Program the ELF with CubeProgrammer and compare by readback. Avoid routine full-chip erase; the final 2 KiB contains the persistent journal. Each MCU requires its own source configuration and build. The public UID-unconfigured source has not been newly flashed as a public image on hardware.

## Publication checks

Release 1.0.1 builds in both Release and Debug with the listed toolchain. A representative nonzero identity build occupies 61,280 of the 61,440 application Flash bytes and reserves 16,368 of 20,480 SRAM bytes, including the 2 KiB stack; heap use is zero. The separately configured reference-device build has the same 61,280-byte load span in both configurations (61,276 section bytes plus four alignment bytes). The default zero-UID build occupies a 56,504-byte load span because activation paths become unreachable; use a configured build when assessing space. Different device constants, tools or configuration can change these figures.

Private software checks passed 46 LED/GPIO/Capture cases, the rolling acquisition suite with trigger handoff cases, and three fresh-process application startup/fault scenarios. Eight ARM-emulated terminal-fault cases checked PA15 shutdown with its clock enabled or disabled on the configured Release ELF. These checks do not establish physical timing or a new hardware qualification. Revision 1.0.1 has not been flashed or tested on a physical board.

## Size optimization and future headroom

The unreleased single-bank revision has a configured Release load span of 60,800 bytes within a 63,488-byte application region, leaving 2,688 bytes. The final 2,048 bytes are reserved for storage. RAM comprises 112 bytes of initialized data, 16,204 bytes of BSS and 2,052 bytes of stack reservation/alignment, leaving 2,112 bytes of the 20 KiB SRAM unallocated. These are configured-build software measurements, not hardware qualification. Persistent payload staging uses 2,016 bytes of owned RAM; no heap allocation was introduced.

Both configurations retain `-Os`, LTO with one partition, section garbage collection and the existing non-LTO Capture translation unit. They now enable `-fipa-pta` for more precise interprocedural pointer analysis; startup and terminal application fault handlers have cold-path hints. No ABI or floating-point semantics were changed.

Controlled comparisons found no saving from `-Oz`, while `-O2` and the tested global no-inline settings were larger. On the same pending-event implementation before the cold-path hints, `-fipa-pta` reduced the load image from 61,520 to 61,304 bytes. The hints then reduced it to 61,280 bytes. A lossless packed-font prototype saved table bytes but increased total code size and was discarded. These are build-size observations, not speed measurements.

The 1.0.1 figures above are historical. The unreleased revision intentionally reduces persistent storage to one 2 KiB bank and expands application Flash to 62 KiB. Saving no longer preserves an earlier committed bank through power loss. Payload staging consumes additional bounded SRAM; the 2 KiB stack reservation is retained. See the changelog and Flash optimization report for current measurements.

GNU make may report missing LTO `.su`/`.cyclo` sidecar targets; a linker warning about an executable writable SRAM load segment reflects the retained Flash-programming kernel. The application compiler treats source warnings as errors. For command-line builds, use the headless builder's `-no-indexer` option to avoid unrelated CDT indexer diagnostics.

The PDF manual illustrates the reference firmware interface. Use this repository's getting-started guide for the public source's per-board configuration and programming procedure.

The unreleased single-bank storage format can lose previously saved data if reset or power fails during a save. The old lower bank is application space; previous saved records are not guaranteed to migrate.
