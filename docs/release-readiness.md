# Release verification

Studio 2.0.2 works with the companion firmware for the classic DSO138 PCB 109-13800-00C with an STM32F103C8T6 and ILI9341. Owners configure their board identity and build the firmware. USB is off by default. See the [Windows release](https://github.com/dodekm/dso138-firmware/releases/latest), [Studio audit](../host/windows/VERIFICATION.md#studio-202-audit) and [build instructions](building.md).

The software-only footprint of the current architecture-refactor worktree is
recorded separately in [Building](building.md#current-architecture-refactor-worktree-build).
It is not a new hardware qualification and does not replace the R6 bench
results below.

## Refactored firmware: reference-board bench check

On 2026-11-07 the private reference-board build was programmed through the
specified ST-Link at 50 kHz, using application pages 0–62 only. The programmer
verified the application image. A read-only hash of the final 1 KiB page matched
before programming, after programming, and after the subsequent checks. No
settings or calibration save was intentionally performed. Three automatic
restarts each recovered the USB INFO response and reached display bring-up
stage 6 without reported startup, acquisition, transport, controller or panel
faults. Observed clocks were 72 MHz system/timer and 12 MHz ADC.
At that point the preserved page was nonblank and its first word was not the
current schema-2 journal magic. The later storage investigation and explicit
save test are documented below.

Using the board test output at DC coupling and nominal 1 V/div, the refactored
Studio session acquired successive 1,024-sample records at 1 ms/div. One record
measured about 1,002 Hz and 3.34 V between its median low and high levels, using
the firmware's nominal calibration rather than an independent voltage reference.
An edge-triggered host Single completed; an unreachable trigger timed out.
Fragmentation, CRC rejection, duplicate replay, conflicting request IDs,
disconnect/lease cleanup, cancellation, and repeated raw downloads also passed
bounded USB checks. One short run measured about 43.5 kB/s of raw sample
payload; it was not continuous streaming. An offscreen instance of the actual
Studio window connected, entered HOLD, changed timebase, displayed live records
and disconnected. This does not test native Windows input delivery or visible
LCD pixels.

For target Spectrum testing, the debugger changed the idle UI domain and
requested redraw in RAM; this bypassed the physical button path. The target's
512-point result matched an independent periodic-Hann DFT of the same held
samples at bin 5, about 999 Hz, within 0.007 dB. The 1,024 raw codes and frozen
timing matched before and after the Spectrum/Scope changes. Four seconds of
USB STATUS traffic during local RUN yielded 7,172 replies and 38 additional
acquisitions and presentations, with no recorded acquisition, transport,
controller or panel fault; the maximum observed begin-to-begin poll interval
was 53,691 cycles. This is a bounded observation, not a worst-case guarantee.

The programmer reported 0.01–0.02 V on its target-reference input despite
successful SWD communication and programming; the analog supply and USB
electrical compliance were not independently measured. A separate private
instrumented build painted the 2,048-byte stack region at reset and observed a
1,012-byte write watermark during startup, acquisition, Spectrum, and USB
capture and fault-handling exercises. It retained 1,036 bytes of untouched
sentinel at the bottom. These are writes observed in that instrumented build,
not a worst-case bound or a watermark of the normal image. The normal image was
then restored and programmer-verified, followed by three more successful
automatic restarts and an offscreen Studio live-capture check. The settings-page
hash still matched the pre-programming value. Compiler stack-usage
output had no dynamic entries, but is not a complete call-chain or interrupt
nesting bound. Physical display appearance, persistent save behavior and
absolute analog/timing accuracy remain unqualified by these checks.

## Storage identity and persistence follow-up

The original contents of the final page included a CRC-valid, board-matched
`DSC1` calibration section with zero measured entries at page offset 672,
but no complete current journal. That offset is exactly where the calibration
section followed a 1,024-sample packed waveform in the older 2 KiB layout:
32 journal-header bytes + 128 record-header bytes + 1,536 waveform bytes from
the old `0x0800F800` start. The current application uses the preceding page,
so an old record's header would no longer be present. This strongly supports
an old-record tail, but the precise write history is not independently proven.
The requested storage test replaced that obsolete tail with a current journal.

An independent startup problem also prevented persistence: on the reference
board, a temporary diagnostic build observed `DBGMCU_IDCODE` as `0x00000800`
without a debugger and `0x20036410` while the debugger remained attached. The
old guard rejected the former and left the storage adapter in Identity fault.
ST's [ES096 errata, section 2.2.2](https://www.st.com/resource/en/errata_sheet/es096-stm32f101x8b-stm32f102x8b-and-stm32f103x8b-mediumdensity-device-limitations-stmicroelectronics.pdf)
states that this register is not available to ordinary user software. The
board and storage guards now use the complete configured UID and 64 KiB Flash
size instead. Device type `0x410` is verified externally by the programmer;
it is not inferred from the unreliable software read.

The corrected private build was programmed and verified with application
pages 0–62 only. Three subsequent resets with no probe session held open through
startup qualified storage and restored settings and calibration in HOLD. A
software ARM emulation of the compiled identity check accepted the configured
UID and 64 KiB size, and rejected each individual UID-word mismatch and 0,
32 and 128 KiB size values.

For bounded hardware persistence checks, debugger commands queued the same
storage actions normally produced by the panel; physical button input was not
retested. Settings saved and restored both with and without a held waveform.
An empty calibration bank saved with the settings preserved. A synthetic,
non-metrological 1 V/div profile was then placed in RAM for testing: SAVE CAL
persisted it, SAVE CONFIG preserved it, RESTORE CAL and a standalone reset
restored its exact fields, and a new capture used those fields. The synthetic
profile was subsequently removed; an empty calibration bank was saved and
verified after reset. The test restored the original 100 µs/div, Auto and
2048-code trigger selections and verified their persistence. No measured
calibration was established. Real power-loss behavior and analog accuracy
remain unqualified.

The original USB protocol header's `UsbStreamPort` source interface was
checked against `studio-v2.0.2` and restored after a header split. A compile
fixture now accepts that original usage, and configured Debug and Release
images are byte-identical to their pre-fix counterparts. This confirms the
header change did not alter those builds; it is not a hardware test.

A Windows x64 differential fixture drove the acquisition owner through a fake
DMA port in both the refactored worktree and `studio-v2.0.2`. A 64-sample
edge-triggered record and a 1,024-sample automatic record spanning both DMA
halves matched in sample chronology, trigger results, callbacks and counters.
Eight physical-update cases, eight injected observation/sample faults and four
failed-start cases also produced matching states and fault results. The fake
port does not validate STM32 register behavior, interrupt timing or analog
sampling.

The firmware screen renderer was compared with `studio-v2.0.2` on Windows x64
using a simulated ILI9341 bus and framebuffer. Full and partial redraws,
low-budget fallback, cancellation and transfer-fault recovery produced matching
framebuffer and bus-operation hashes for both synchronous and simulated
asynchronous pixel transfers. This does not verify the physical display bus,
DMA timing or panel appearance.

An additional controller integration comparison used fake DMA and LCD buses
with `studio-v2.0.2` and the refactored worktree. Both handled a settings
override and stale-revision rejection, then captured and rendered a 1,024-sample
host Single record with the same trigger, measurement and framebuffer results.
The same raw samples remained exportable after switching to Spectrum; a
physical-setting change invalidated the export lease, and a pending host job
could be cancelled. Service counts and simulated port traces also matched. This
is a software simulation, not a physical timing or analog measurement.

The same controller fixture compared firmware USB request handling with the
baseline using a fake byte stream. Responses matched for three-byte input
fragments, temporary transmit backpressure, CRC rejection, request replay and
ID conflict, settings writes and rollback, sample reads across the trigger,
lease release and disconnect, idle timeout, and a timed-out capture request.
The test checks protocol and ownership behavior in software; it does not
exercise USB enumeration or hardware interrupt timing.

A host-side differential check compared the refactored calibration and session
code with tag `studio-v2.0.2`. Both produced identical results for 480
combinations of physical scale, generation, coupling and board identity, and
12 invalid calibration summaries. A three-reference DC fit, installation,
DSC1 export and import also produced identical results and record bytes. This
checks those cases on Windows x64; it does not qualify the complete calibration
workflow, target timing or physical accuracy.

A separate Windows x64 comparison with `studio-v2.0.2` exercised 1,440
settings-header inputs; both versions accepted and round-tripped the same 432
and produced identical encoded bytes. For an initial save to an erased simulated
page, all 633 save-step boundaries were rescanned as restart snapshots; journal
states and results matched the baseline. The comparison also flipped one bit in
each of the 68 header/payload bytes of the completed record and compared scan
results. Those initial-save snapshots did not simulate a partially completed
Flash operation or replacement of an existing record.

A further simulated replacement compared all 633 save-step boundaries with the
baseline: 19 snapshots exposed the old record, 580 exposed no valid record and
34 exposed the new record. Both versions also matched for 93 injected program
failures at 31 locations, including partial-byte and completed-but-reported-failed
writes, and eight interrupted or reported-failed erase cases. No tested scan
published a mixed payload. These are selected simulated failure patterns, not an
exhaustive model of physical Flash behavior. The single-page format can lose the
previous record during an interrupted save.

The Studio protocol codec was also compared with `studio-v2.0.2` on Windows
x64 using 660 encoded requests, all 21 split points of one reply, ten status
values, seven corrupt/unrelated-frame recovery cases, and 680 mutations of
valid settings and base/extended metadata responses. Encoded bytes, decoded
fields, errors and reply results matched for those inputs. This comparison does
not cover the full session state machine or a physical USB connection.

A fake-transport session comparison with `studio-v2.0.2` exercised the default
constructor, fragmented handshake, settings readback and deferred write, a held
45-sample download, malformed lease-release recovery, reconnect, Single capture
and cancellation while its acknowledgement was pending. Request bytes, observed
signals and published samples matched for those scenarios. The test also links
the refactored CMake `dso_session` target directly; it does not establish serial
driver timing or physical USB behavior.

Studio's refactored capture, measurement and spectrum code was compared with
`studio-v2.0.2` on Windows x64 for 77 synthetic captures and 462 combinations
of spectrum window and DC policy. Reported measurements, FFT bins and peaks
matched for those cases, including invalid and clipped inputs. As an independent
sanity check, a 32-sample square-wave period at 500 kS/s measured 15,625 Hz and
50% duty cycle. Twenty-eight native captures round-tripped, and their native
and CSV export SHA-256 digests matched the baseline. These checks do not cover
the complete GUI or every possible signal and file input.

An offscreen Qt fixture compared the refactored Studio window with
`studio-v2.0.2` using the same Windows compiler, Qt version, Fusion style and
font. Rendered-image SHA-256 hashes and visible-control snapshots matched at
startup, after selecting Measurements and Spectrum, after enabling cursors,
and after resizing the window. The fixture changes tabs and the cursor action
programmatically.

A second offscreen fixture dispatched QtTest mouse and keyboard events in both
builds. Refresh generated one click on an inside release and none on an outside
release; switching analysis tabs, selecting the Blackman window by keyboard,
and selecting Show cursors from the View menu produced the expected signals.
The rendered states and signal counts matched the baseline. These synthetic
events do not verify Windows input delivery, live capture or connected-device
controls.

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

The static audit resolved 272 reachable nodes and 61 indirect call sites with no unresolved targets or missing frames. HardFault and NMI use register-only containment. The R6 linker saves 48 bytes from R5 by discarding unused process-exit finalizers. Startup constructors, local scope cleanup and explicit stop/fault cleanup remain. Release and Debug ARM source compilation completed without source errors or warnings. CubeIDE emitted the generated Make sidecar-target and CDT scanner diagnostics described in [building](building.md); the linker may also warn about the writable/executable SRAM load segment used by the retained Flash-programming kernel.

## Repeat the bench checks

1. Configure your board identity and build both configurations in clean CubeIDE copies. Compare the binary bytes and check the application load span and stack in the linker map. Follow the [build and programming instructions](building.md) to preserve the last page. The reference R6 run verified programming of pages 0–62 and compared the application through SWD in memory.
2. Capture the onboard test output and select HOLD. Use **Download held** in [DSO138 Studio](../host/windows/README.md) and save the capture. Repeat in Spectrum and after returning to Scope. Compare raw samples and frozen metadata while excluding the per-download lease token. Compare the target peak with a mean-removed periodic-Hann DFT of its 512-sample window. See [Spectrum conventions](spectrum-design.md).
3. With USB enabled for the bench setup, exercise restart, held export, host Single, repeated captures and cancellation. Inspect job/lease status and try a fresh export after reconnecting the serial session. Check that the reference capture settings and fault counters remain consistent. See [USB protocol and limits](usb-protocol.md).

## CubeMX and qualification limits

The `.ioc` records the MCU, clocks, ADC1/ADC2, TIM2, ADC1 circular DMA, NVIC, selected GPIO and SWD. It omits the source-owned LCD timer/DMA transport, USB IP configuration and the storage linker layout. Its 48 MHz USB clock entry is a clock value. The checked-in project owns C++ startup, MSP and interrupts. `ProjectManager.NoMain=false` and `ProjectManager.DeletePrevious=true` can introduce competing generated files. Import and build the existing project. CubeMX regeneration and merge remain unsupported.

The CPU and acquisition timer are configured at 72 MHz, the ADC at 12 MHz and USB at 48 MHz. The onboard test output shares the MCU clock. Absolute timebase and trigger timing, input-voltage accuracy, analog bandwidth, physical LCD pixel appearance, and sustained sample continuity remain outside the bounded checks. The reference calibration was nominal. The Spectrum timing is an inclusive debugger-observed presentation interval. Separate records have acquisition and display dead time. Continuous streaming is outside this release.

The stock USB trial uses the fixed pull-up and assumes host VBUS. Production electrical qualification and physical suspend/resume tests remain outstanding. DTR session recovery was observed independently of the simulated bus suspend/resume cases. The USB connection joins PC and oscilloscope grounds. See the [hardware contract](usb-hardware.md).
