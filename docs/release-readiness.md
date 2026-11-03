# Release verification

The current release pairs DSO138 Studio 2.0 with companion firmware for the classic DSO138 PCB 109-13800-00C with an STM32F103C8T6 and ILI9341. Owners configure their board identity and build the firmware. USB is off by default. See the [Windows release](https://github.com/dodekm/dso138-firmware/releases/tag/studio-v2.0.0) and [build instructions](building.md).

## Current Studio 2 reference results

The USB-enabled companion firmware uses 64,496 of 64,512 application
bytes. Programming verified the application and preserved the final
1 KiB settings and calibration page. The board passed remote HOLD,
timebase and trigger writes, raw exports at three timebases, settings
rollback and a bounded 500 kS/s acquisition test under USB status
traffic. At 1 ms/div the Studio window displayed 4.55 records/s in the
reference run. Live view uses separate acquisitions with gaps between
records. See the [Studio 2 verification](../host/windows/VERIFICATION.md).

## R6 firmware qualification

The following Flash totals and stack watermark apply to the preceding
R6 firmware.

| Requirement | Verification |
| --- | --- |
| Acquisition and controls | Timer-triggered ADC/DMA, 18 timebases from 10 µs/div through 5 s/div, Auto/Normal/Single, RUN/HOLD, trigger position, the physical controls and D3 are retained. The operator previously confirmed the controls and trigger LED on the reference board. R6 produced repeated records at the configured 500 kS/s rate in Scope and Spectrum. |
| Rendering and measurements | R6 completed waveform and Spectrum LCD presentations. Native tests passed 120 independent mean/RMS comparisons across offsets, gain shifts, sample lengths and all timebases, plus 10,000 quotient/remainder comparisons. The observed maximum mean and RMS differences were 0.470588 µV and 3.717567 µV on the numerical vectors. |
| Spectrum and record ownership | Native tests passed 2,316 FFT bin comparisons, nine analyzer edge cases and 200 controller acquire/analyze/render cycles. On R6, the dominant bin and level agreed with an independent DFT of the held slice within 0.0022 dB. Scope/Spectrum/Scope exports matched all 1,024 codes and all 16 frozen metadata fields. A debugger watchpoint verified cancellation after workspace acquisition while DMA was stopped, followed by a released workspace, restored Scope projection and unchanged held data. |
| Acquisition under USB load | Two successive four-second RUN windows received 7,399 Scope and 8,694 Spectrum status replies. The windows yielded 66 and 68 acquisitions respectively. There were no recorded acquisition, transport, panel or controller faults. The largest observed poll gap was 53,764 cycles (747 µs at 72 MHz) against the 69,631-cycle budget (967 µs). The host Single request returned Busy while RUN was active. |
| USB | Three debugger-controlled restarts reached the display and Windows CDC handshake. Live checks passed fragmented requests, replay, conflicting IDs, CRC recovery, held-frame exports, Single, cancellation, timeout and stale-token rejection after DTR deassertion and reopen. CDC fakes covered control-transfer ordering, endpoint halt, suspend/resume and stale session state. A register-write oracle covered 16,777,216 endpoint combinations. |
| Settings and DC calibration | The CRC-checked format saves settings and up to nine DC calibration profiles in one 1 KiB page. Native tests passed round trips, old-format rejection, counterpart preservation and 268 interrupted-write reboot cases. Boot leaves the page intact. Interruption during an explicit save can lose the previous record. |
| Builds and delivery | Clean configured CubeIDE Release and Debug builds produced matching application bytes. The source includes the CubeIDE project, the audited `.ioc` reference, MIT and third-party notices, and the user manual. |

## Flash, SRAM and stack

| Resource | R6 USB bench profile |
| --- | --- |
| Application Flash | 64,408 of 64,512 bytes, 104 bytes free. Loadable span ends at `0x0800FB98` before the storage page at `0x0800FC00–0x0800FFFF`. |
| SRAM | 112 bytes `.data`, 13,192 bytes `.bss`, 2,048 bytes reserved stack, 5,128 bytes unallocated. Heap zero. |
| Static stack | 1,296-byte maximum foreground chain plus 212 bytes of enumerated exception and IRQ nesting gives a 1,508-byte model. A further 36-byte allowance gives a 1,544-byte engineering budget and 504 bytes of margin. |
| Target watermark | 928 written bytes observed after reset-time stack painting and the exercised startup, FFT, rendering and CDC workloads. The watermark measures changed bytes in those runs. The static budget covers paths outside the observed workload. |

The static audit resolved 272 reachable nodes and 61 indirect call sites with no unresolved targets or missing frames. HardFault and NMI use register-only containment. The R6 linker saves 48 bytes from R5 by discarding unused process-exit finalizers. Startup constructors, local scope cleanup and explicit stop/fault cleanup remain. The Release and Debug compiler and linker completed without ARM source errors or warnings. Known generated make sidecar and host CDT scanner diagnostics are described in [building](building.md).

## Repeat the bench checks

1. Configure your board identity and build both configurations in clean CubeIDE copies. Compare the binary bytes and check the application load span and stack in the linker map. Follow the [build and programming instructions](building.md) to preserve the last page. The reference R6 run verified programming of pages 0–62 and compared the application through SWD in memory.
2. Capture the onboard test output and select HOLD. Use **Download held** in [DSO138 Studio](../host/windows/README.md) and save the capture. Repeat in Spectrum and after returning to Scope. Compare raw samples and frozen metadata while excluding the per-download lease token. Compare the target peak with a mean-removed periodic-Hann DFT of its 512-sample window. See [Spectrum conventions](spectrum-design.md).
3. With USB enabled for the bench setup, exercise restart, held export, host Single, repeated captures and cancellation. Inspect job/lease status and try a fresh export after reconnecting the serial session. Check that the reference capture settings and fault counters remain consistent. See [USB protocol and limits](usb-protocol.md).

## CubeMX and qualification limits

The `.ioc` records the MCU, clocks, ADC1/ADC2, TIM2, ADC1 circular DMA, NVIC, selected GPIO and SWD. It omits the source-owned LCD timer/DMA transport, USB IP configuration and the storage linker layout. Its 48 MHz USB clock entry is a clock value. The checked-in project owns C++ startup, MSP and interrupts. `ProjectManager.NoMain=false` and `ProjectManager.DeletePrevious=true` can introduce competing generated files. Import and build the existing project. CubeMX regeneration and merge remain unsupported.

The CPU and acquisition timer are configured at 72 MHz, the ADC at 12 MHz and USB at 48 MHz. The onboard test output shares the MCU clock. Absolute timebase and trigger timing, input-voltage accuracy, analog bandwidth, physical LCD pixel appearance, and sustained sample continuity remain outside the bounded checks. The reference calibration was nominal. The Spectrum timing is an inclusive debugger-observed presentation interval. Separate records have acquisition and display dead time. Continuous streaming is outside this release.

The stock USB trial uses the fixed pull-up and assumes host VBUS. Production electrical qualification and physical suspend/resume tests remain outstanding. DTR session recovery was observed independently of the simulated bus suspend/resume cases. The USB connection joins PC and oscilloscope grounds. See the [hardware contract](usb-hardware.md).
