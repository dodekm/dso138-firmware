# Flash optimization investigation

## Baseline

The starting point is v1.0.1, commit `e5726c6292fb49093e6178c31a536faac4f8ef29`, built for the reference board with STM32CubeIDE 2.2.0 and GNU Tools for STM32 14.3.rel1. These numbers come from the configured Release ELF, not the zero-UID public configuration. Its load span is 61,280 bytes of the 61,440-byte application region, leaving 160 bytes. The final 4 KiB of the 64 KiB device is reserved for the settings journal.

`arm-none-eabi-size -A` reports 268 bytes of vectors, 57,704 bytes of text, 3,168 bytes of read-only data, 8 bytes of exception index, 16 bytes of initialization/finalization arrays, and 112 bytes of initialized RAM data with a Flash load image. Alignment accounts for four more bytes. RAM includes 14,204 bytes of BSS, 112 initialized bytes, and 2,052 bytes of stack reservation and alignment, leaving 4,112 bytes of the 20 KiB total. The writable executable SRAM segment includes the Flash-programming kernel; its permissions must not be changed without validating that functionality.

The largest individual symbols include `dso_app_idle.part.0` (9,136 bytes), `ScopeController::service_storage` (3,672), `main` (3,668), and `ScopeController::format_scene` (3,576). LTO inlines and specializes code into these symbols, so their sizes do not represent independent module costs. The 754-byte `__udivmoddi4` helper also warrants investigation; removing one caller will not save it if other required operations still depend on it. Exception handler symbols alias the same fault entry and must not be counted repeatedly.

The existing build already uses `-Os`, LTO with one partition, `-fipa-pta`, section garbage collection, Newlib-nano, and disabled C++ exceptions and RTTI. `dso_capture.cpp` is deliberately compiled without LTO. Earlier v1.0.1 experiments found `-Oz` did not improve the image, `-O2` was substantially larger, and a packed font increased total size. Those results are specific to their measured source and toolchain.

This baseline is a size measurement, not evidence that every runtime path meets its timing budget. Any proposed change needs a configured build, comparison of the complete load span and RAM use, appropriate behavior tests, and timing evidence for affected real-time paths. The storage scratch-buffer change below is accepted on software evidence; other candidates remain experimental.

## Formatter experiment: explicit outlining

An isolated configured Release build added `noinline` to `Text::add`. Its complete ELF was byte-identical to the baseline (SHA-256 `119c9c2d1215eaaf38ac686d7f18767c2213f3d27a6fd989830bdba19836719a`), with the same 61,280-byte load span. `nm` shows that the compiler had already outlined this function as a single 100-byte symbol. The annotation was discarded; it would add a compiler-specific constraint without changing the image. This experiment does not establish anything about other formatting functions or inlining choices.

The same independent experiment on `Text::decimal` also produced a byte-identical ELF. The compiler already emits one 124-byte function. This annotation was discarded too.

An isolated `noinline` annotation on `ScopeController::format_measurements` produced a 61,240-byte configured load span, saving 40 bytes. The compiler's stack-usage files report a 192-byte `format_scene` frame in both builds, plus a new 56-byte `format_measurements` frame in the candidate. The candidate also adds a call on the rendering path; its timing and full call-chain stack effects have not been measured. It remains an unaccepted experiment: the small Flash saving does not by itself justify reducing stack headroom or assuming the runtime effect is negligible.

Replacing the single-character `Text::add` calls in numeric formatting with direct bounded writes increased the configured load span to 61,296 bytes, 16 bytes larger than baseline. This candidate was discarded without a runtime-performance claim; it duplicated dirty-range and old-label handling and did not justify the additional source complexity.

## Switch lowering experiments

Two isolated configured Release builds changed compiler and linker flags across the project while retaining the existing per-file Capture exception and all feature settings. Adding `-fno-jump-tables` increased the load span to 61,368 bytes, 88 bytes larger than baseline. Adding `-fno-tree-switch-conversion` instead increased it to exactly 61,440 bytes, leaving no application headroom: read-only data shrank by 168 bytes but text grew by 328 bytes. Neither option was retained. These measurements do not establish their effects on execution time, individual functions, or other flag combinations.

## CRC sharing experiment

Storage and scope storage contain identical CRC-byte functions. `nm` shows that LTO already places both names at the same address, so their apparent source duplication does not imply duplicated machine code. An isolated candidate also routed calibration CRC bytes through that shared function. Its configured load span was 61,272 bytes, eight bytes smaller than baseline, with unchanged static RAM. This adds a function call per calibration byte and introduces a compiler-specific outlining constraint; neither its timing nor its full storage/calibration behavior has been validated. The eight-byte saving does not justify accepting it. The existing CRC code and on-Flash formats remain unchanged.

## Linker and runtime review

The installed GNU linker does not advertise identical-code folding. Adding its `--relax` option to the existing configured build yielded an ELF byte-for-byte identical to baseline; the option was not retained. This does not assess another linker or toolchain.

Linker-wide section sorting by alignment produced a 61,244-byte load span (36 bytes less than baseline); sorting by name produced 61,296 bytes (16 bytes more). Both were isolated configured Release builds with the standard linker script and storage bounds. Alignment sorting changes the code layout, so its effect on Flash-fetch timing has not been established and it is not yet accepted. The name-sorting candidate was discarded.

The configured ELF retains `__libc_init_array` (72 bytes), which startup calls for static constructors, and a global destructor entry in the retained finalization array. It has no linked `malloc`, `free`, `operator new`, or `__cxa` symbol in this build. `memset` and `memcpy` occupy 16 and 28 bytes respectively. The 754-byte unsigned 64-bit division helper is reached through the ABI division veneer; disassembly has 38 branch-and-link calls to the division entry points. Removing a single division use cannot by itself eliminate this helper. Any replacement must preserve all callers' arithmetic and zero-divisor semantics, and demonstrate an acceptable timing trade-off. Removing static constructor or finalization infrastructure requires a separate lifecycle audit; these are not treated as dead solely because the main loop normally runs indefinitely.

## Source review: storage, UI, and peripheral setup

### Linked-file ranking

Baseline map input ranges were merged before summing, because pooled strings overlap and summing raw map sizes overcounts Flash. Ranked contributions below include initialized RAM's Flash load image; 48 bytes of alignment complete the 61,280-byte load span.

| Linked object or archive member | Flash bytes |
| --- | ---: |
| LTO combined application/HAL object | 58,826 |
| libgcc unsigned 64-bit division object | 764 |
| dso_capture.o | 532 |
| dso_fault_entry.o | 472 |
| startup_stm32f103c8tx.o | 342 |
| crtbegin.o | 72 |
| Newlib initialization | 72 |
| libgcc division ABI veneer | 48 |
| memcmp | 32 |
| memcpy | 28 |
| memset | 16 |
| crtn.o | 16 |
| crti.o | 8 |
| libgcc division fault support | 4 |

LTO combines source files, so the first row cannot honestly be presented as an additive ranking of original C++ modules. Its largest code symbols guide source investigation instead: application idle (9,136 bytes), storage service (3,672), main (3,668), scene formatting (3,576), acquisition arming (2,058), global initialization (1,740), storage step (1,376), and raster span (1,152). These are optimization targets, not evidence that their work is unnecessary. The remaining memory library functions total only 76 bytes; replacing them offers little headroom and would add maintenance cost.

The linked image has no allocator, global new/delete, RTTI/vtables, formatted I/O, or C++ exception runtime symbols. Unused HAL input sections are discarded; merely excluding their source files does not establish any Flash saving. Required HAL code is also inlined into application functions, so an out-of-line HAL symbol list is not its full cost.

Independent source reviews found that the storage service's payload-read and publishing loops have deliberately different work budgets. They must not be combined merely to shorten source. Calibration import validates the entire record before mutating the calibration bank; merging those passes would change failure atomicity. Neither change is proposed.

UI label mappings and initialization guards contain apparent repetition, but source inspection alone does not prove duplicated machine code. The analog-power and test-output clock predicates appear equivalent; other drivers enforce different conditions. A common clock helper is not accepted without a full-image saving. Acquisition preparation already shares backend setup, and unused HAL sections are eligible for linker garbage collection.

The measured source candidates were an unnecessary scratch-buffer initialization and selective outlining of controller methods fused by LTO. Their outcomes are recorded below.

The outlining review confirmed that `arm`, `redraw`, and `service_storage` already have separate symbols in the baseline. An independent `noinline` experiment on `ScopeController::service` increased the image by 32 bytes against an unmodified build in the same workspace. It also changed stack frames (baseline idle 384 bytes and main 184; candidate service 344 and main 216). The annotation was rejected. A preliminary comparison used an inconsistent baseline; the same-workspace rebuild supersedes that comparison.

## Accepted: remove redundant storage scratch clearing

The Tail phase now leaves its 32-byte scratch buffer uninitialized. A successful selected-calibration read fills the requested bytes, the CRC consumes only those bytes, and a failed read returns before consumption. The STM32 storage reader writes every requested output byte on success. Restoring a calibration bank uses the existing DMA staging buffer instead.

An isolated configured Release build reduced text by 16 bytes with unchanged read-only data and static RAM, reducing the load span from 61,280 to 61,264 bytes and increasing application headroom from 160 to 176 bytes. Disassembly removes the Tail-path memset call and setup; the function stack frame is unchanged. Existing native storage-UI and calibration-persistence-UI suites passed against the candidate, covering partial-read failure isolation, malformed records, reboot/restore and staged publication. This demonstrates removal of work on that path; no hardware timing measurement or deployment has been performed.

## Accepted: compact UI label selection

Four switch helpers replace inline ternary ladders for storage action/status, calibration action, and measurement page labels. All labels and invalid-enum fallbacks are preserved. LTO emits no additional helper calls; the scene-formatting frame remains 192 bytes. Independently this saves 40 bytes (80 less text, 40 more read-only data).

A configured build combining these helpers and the scratch change has a 61,224-byte load span, ending at 0x0800EF28: 56 bytes saved against baseline and 216 bytes of application headroom. Static RAM and the inspected idle/main/scene/storage stack frames are unchanged. Existing native controller UI, storage UI, and calibration-persistence UI suites passed using the complete candidate production source set. These are software results; the candidate has not been flashed or timed on hardware.

## Additional consolidation candidates

Sharing base capture-field validation between acquisition and saved-record decoding passed six relevant native suites, but a clean target comparison produced identical section sizes and a 61,280-byte span. The differing Auto-timeout policies remained separate. This refactor was not adopted as a footprint optimization.

Sharing signed rounding across scaling, measurements, and calibration saved 160 bytes in an isolated target build and passed six numeric suites. However, LTO introduced calls in foreground projection, a 16-byte helper frame, and an increased idle frame. That candidate is unaccepted pending evaluation of an inline variant and its performance consequences.

Forcing the shared rounding helper inline removed its calls and restored the baseline idle frame, but increased the image by 32 bytes against baseline. That variant was rejected. The ordinary shared version remains unaccepted because its projection-path timing has not been established.

A symmetric FIR prototype stored only half the coefficients and paired symmetric history samples. A bit-exact oracle passed 78,660 input calls across adversarial/random streams, clipping, invalid-input latch and reset cases; three decimator/transition suites passed. However, 56 fewer bytes of read-only data were offset by 56 more code bytes, leaving the full load span unchanged. It was not adopted for this Flash objective. Fewer multiplies alone do not establish target execution time.

## Display window encoding

The portable LCD driver encoded the same coordinate bytes separately for the optional sequence transport and byte fallback. One shared 11-byte packet now feeds both paths, retaining the sequence count/mask and the fallback's two four-byte data loops and command order. An isolated configured build saved 48 bytes; the window function shrank from 254 to 206 bytes with unchanged static RAM. Existing protocol/GRAM, asynchronous transfer/cancellation/fault/fallback and rectangle suites passed. Aggregate measurements with the other accepted source changes follow when verified.

The aggregate configured image with scratch clearing removed, compact label selection and shared LCD encoding ends at 0x0800EEF8: 61,176 bytes, saving 104 bytes and leaving 264 bytes of application Flash. Initialized RAM (112 bytes), BSS (14,204 bytes), and stack reservation/alignment (2,052 bytes) are unchanged. Inspected frames remain idle 384 bytes, main 184, scene 192, storage service 224 and LCD window 40. The sequence fast path retains the same callback sequence and packet. UI/storage/calibration and display/async/rectangle suites passed against the aggregate source. No hardware timing claim or deployment is made.

## Further compiler configuration comparisons

Independent builds on the scratch-and-label source (61,224-byte span) produced: `-fno-ipa-sra` 61,288 bytes; `-fno-partial-inlining` 61,256; `-fno-ipa-cp-clone` 61,224; and `-flto-partition=none` 61,232. Static RAM was unchanged. None was adopted. These results precede the display encoding change; they do not prove the behavior of combinations or different source revisions. GCC reports that IPA cloning is disabled at `-Os`, so explicitly enabling it warrants a separate experiment.

Explicitly enabling IPA cloning on the aggregate source produced the same 61,176-byte load span and unchanged static RAM. No compiler flag from this additional batch was adopted.

## Larger candidates investigated

Selected-sample reads repeatedly call the 588-byte full saved-record validator, despite open-time semantic validation. A private candidate reused the existing journal decoder after checking publication lifetime and retained sample bounds, transport failure handling and final publication CRC. The sample-reading function shrank from 186 to 108 bytes, and storage corruption/power-loss/index/output-preservation suites passed. However, the validator remains necessary during open, and the total image grew by 16 bytes to 61,192. It was not adopted for this footprint objective; its possible runtime benefit is distinct from Flash savings.

The division library already uses Cortex-M3 CLZ/UDIV and limb quotient corrections; replacing it with a compact bitwise loop would risk slower worst-case arithmetic. No replacement was adopted. Selective installed-tool searches found only the GNU Arm 14.3.1 target compiler; alternative toolchain evaluation is still outstanding. A preliminary dictionary-packing experiment on UI strings estimated about 230 bytes of gross saving before decoder/table costs; that is an experiment-specific estimate, not an upper bound on possible compression.

## Source attribution and startup review

A private analysis build added debug information while retaining Release optimization. Its objcopy load image is byte-for-byte identical to the accepted 61,176-byte image (SHA-256 `d11c70734ce316ecc215c1296c6d8f34573803a89650791faab6bd780056faaa`). DWARF line attribution within the 9,136-byte idle routine assigned instruction bytes to the following source files. This describes compiler line attribution, not independent or additive module costs; inlining and unattributed instructions limit its interpretation.

| Source file | Attributed instruction bytes within idle |
| --- | ---: |
| dso_ui_controller.cpp | 1,770 |
| dso_measurements.cpp | 1,680 |
| dso_rolling.cpp | 1,092 |
| dso_calibration.cpp | 896 |
| dso_scope_screen.cpp | 772 |
| dso_scaling.cpp | 568 |
| dso_panel_sampler.cpp | 472 |

Inspection found distinct bounded processing and state paths, rather than a duplicated full acquisition pipeline. State reads on either side of service calls observe different points in time; merging them would require a separate concurrency argument.

The global initializer occupies 1,740 bytes with a 224-byte frame. Its 20 memset calls clear 7,408 bytes across member initialization, with a separate 72-byte memcpy. These are runtime bytes written, not Flash bytes consumed. Many initialize distinct buffers and state. Removing them merely because storage begins in BSS would change normal object-construction semantics unless additional lifetime rules or special constructor paths were introduced. No such change was adopted, and no substantial whole-image saving was demonstrated by that review.

## Feature attribution before the single-bank revision

The same debug analysis classified unique instruction addresses throughout `.text` by their active DWARF source file. The following are source-area costs, not independent feature removal savings. In particular, controller code also orchestrates storage, calibration and acquisition workflows; shared/inlined code cannot be cleanly charged to just one user feature.

| Source area | Attributed instruction bytes |
| --- | ---: |
| UI/controller | 11,068 |
| Display, rendering and STM32 LCD | 10,262 |
| Acquisition, triggering, rolling, filtering, scaling and timebase | 9,978 |
| Storage, saving and recall | 7,648 |
| Platform, HAL and initialization | 7,228 |
| Calibration | 2,838 |
| Measurements | 2,144 |
| Shared runtime | 872 |
| Physical controls | 634 |

The attributed subtotal is 52,672 bytes. Another 1,488 instruction bytes lack source-file attribution, and `.text` contains 3,400 bytes of literal pools/gaps. These total the 57,560-byte text section. Constant data, vectors and other load sections are counted separately in the full load-span measurement. This ranking predates the storage geometry/staging change.

## Alternative compiler: GNU Arm 13.3.rel1

An official Windows bare-metal Arm GNU 13.3.rel1 package was evaluated separately from the installed STM32 GNU 14.3 toolchain. Its archive matched the download response's Content-MD5; this is transport-integrity evidence, not an independently signed checksum. An initially long installation path caused standard-header lookup failures. Invoking the same extracted compiler through a short private path resolved the probe and permitted a full rebuild without custom standard-library include paths.

Only the unsupported diagnostic-sidecar option `-fcyclomatic-complexity` was removed; source, target/ABI and optimization settings were retained. The linker mapped an image ending at 0x0800F0B8 (61,624 bytes): 448 bytes larger than the accepted pre-single-bank source, and 184 bytes beyond its 60 KiB application limit. The link was correctly rejected. GNU 13.3 was not adopted. Release information is available from [Arm's official downloads](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads).

## User-authorized trade-off: one storage bank

The user explicitly accepted loss of previous-record protection during saving. The application region is now 62 KiB (63,488 bytes), and only the final 2 KiB at 0x0800F800 is reserved for storage. The old lower bank becomes application space; migration of the latest record from the former layout is not guaranteed. Full 1,024-sample waveform records, settings and calibration remain supported. CRC/readback/commit checks remain, so interrupted records are rejected even though an older record may no longer survive.

Settings and calibration saves may source an existing waveform from the same bank. Therefore the complete outgoing payload is first staged in a fixed 2,016-byte RAM buffer, with at most one two-byte source read per budgeted staging operation. Only successful completion invalidates the previous descriptor and begins erase. No allocation or full-record stack buffer is used. The linker and STM32 adapter both enforce the new storage range. The application-object SRAM budget assertion increases from 14 to 16 KiB to account for the explicit staging allocation; the 2 KiB stack reservation is retained.

The configured GCC 14.3 Release image ends at 0x0800EEA8 and occupies 61,096 bytes: 80 fewer bytes than the preceding accepted source. Together with the additional 2,048 bytes of application space, headroom increases from 264 to 2,392 bytes. Relative to the initial 160-byte headroom, the net increase is 2,232 bytes. Initialized RAM remains 112 bytes; BSS grows to 16,212 bytes. With 2,052 bytes of stack reservation/alignment, 2,104 bytes remain unallocated. The target storage object is 2,108 bytes; host ABI size differs. The largest individual compiler-reported frame remains 384 bytes; this alone is not a full call-chain stack proof.

A private native oracle exercised all 949 erase/program mutation boundaries in three interruption modes (2,847 injected interruptions), accepting only an intact old record, intact new record or no valid record. It checked CRCs, 1,024-sample and calibration preservation, same-bank source aliasing, source-read failure before erase, read-fault retry, representative post-failure resaves, and operation/address bounds. A fresh controller UI test also passed against the new production sources. These are software simulations; this revision has not been flashed or physically tested.

Fresh storage-UI and calibration-persistence-UI suites passed against the target candidate source set. Only the private test expectation for rejection of a malformed sole record changed: it now expects no valid record instead of falling back to a previous bank. Boot HOLD, reboot/recall, frozen provenance, partial-read isolation, independent calibration persistence and waveform/settings preservation remained checked. All seven changed target firmware files were hash-compared against the canonical source after building. Test sources and temporary artifacts remain outside the release repository.

## Accepted: simplify single-bank state

The single-bank implementation still retained mutable bank-address and selection state from the older journal. It now owns one record directly. Bank zero remains the only valid public descriptor index; selection still validates the descriptor and busy/mounted state but stores no redundant selector. A compile-time assertion explicitly limits this representation to one bank. Existing generation, source, bounds, format, CRC, readback and commit checks remain.

The 2,016-byte staging array no longer has redundant zero initialization. Every consumed byte is filled by successful staging reads before erase/programming starts. Failed reads stop staging before use; retries restart from offset zero, and an odd final payload byte uses explicit erased padding rather than reading another staged byte.

The configured load span falls from 61,096 to 60,864 bytes, saving 232 bytes and leaving 2,624 bytes in the 63,488-byte application region. BSS decreases by eight bytes to 16,204; initialized data remains 112 and stack reservation/alignment remains 2,052, leaving 2,112 bytes unallocated. The target storage object is 2,104 bytes, and the largest static frame remains 384. Relative to the initial 61,280-byte source, code/load savings total 416 bytes; the separate 2,048-byte partition recovery accounts for the rest of the headroom increase.

Fresh native tests against hash-matched final sources passed the 949-boundary, three-mode power-cut oracle and storage/calibration UI suites. Additional cases covered payload lengths 1, 2,015 and 2,016, odd erased-byte padding, invalid bank descriptors and read/save bounds, and a partially written failing source followed by a different-length staging retry. An initially stale result log was identified during integration and replaced by fresh final-source runs; the host storage object then measured 2,128 bytes, consistent with the changed class layout. Only the fresh runs support this milestone. No hardware deployment or target timing measurement is claimed.

The display investigation found that the portable `clip_rect` utility is not linked into the target image, so rewriting it cannot reduce target Flash. An official newer GNU Arm release was located, but its public download returned a verification challenge and the older official CDN paths returned 404; no newer-compiler size result is claimed. The existing validated GCC configuration is retained.

## Final configuration and stack checks for cab7e80

Fresh configured Release and Debug builds produced byte-identical 60,864-byte runtime images (SHA-256 `351d01e31ac954bf7675214d87f9faf043900a67d70eac0cce4ecaf2002c2945`). The public zero-identity Release/Debug runtime images were also identical at 56,080 bytes. All builds used their local 62 KiB linker script, retained Capture's non-LTO rule, and completed without compiler errors. Private/public application sources differed only in the per-board configuration header; activation build defines also differ by profile. Line-ending differences were normalized before comparison.

An exact-image GCC callgraph/stack analysis mapped 60 context-specific callback edges to the configured STM32 bindings. The maximum foreground chain totaled 1,544 bytes. Conservatively adding 212 bytes for enabled IRQ software frames and up to five nested exception frames with alignment allowance gives 1,756 bytes against the retained 2,048-byte reservation, a 292-byte modeled margin. Terminal fault assembly makes no software stack allocation or calls. This bound depends on these exact bindings, compiler output and interrupt priorities; it is not a general bound for future configurations.

Actual ARM code emulation compared the accepted LCD window routine with the original in 64 scenarios. Return values, latched status and bus transactions matched for origin/interior/full-panel/maximum-single-pixel rectangles and all fallback-byte failure positions. Fast sequence success took 41 instructions versus 44 originally; sequence failure 58 versus 59; selection failure 31 in both; fallback success 225 versus 219. Callback bodies were excluded. These are instruction counts, not clock-cycle or physical display-refresh measurements.

## Deployment observations for cab7e80

The configured image was programmed with the original probe at 50 kHz SWD and passed programmer verification. The newer saved record in the former lower bank was CRC/identity checked, migrated to the retained upper bank, and compared byte-for-byte before application programming. No full-Flash backup was created. An initial post-program startup reported an analog-power ownership guard; a separate reset/run cleared that condition. Subsequent observations showed startup stage 6, ILI9341 identification and zero application/acquisition/transport/controller/panel fault fields, with advancing frame and screen counters.

Six coherent running telemetry samples covered 128 screen updates over 6,733 ms, or 19.011 updates/s, with unchanged physical-settings generation. Since-boot maximum running service, poll-gap and snapshot counters corresponded to 619.931, 621.222 and 71.056 microseconds at 72 MHz. The input and UI settings were not controlled as a comparative benchmark. These observations do not establish external sampling jitter, amplitude accuracy, CPU occupancy or an improvement over the original firmware. Further probe debugging was stopped at the user's request.

## Additional size-only experiments

Three independent current-source compiler builds measured: `-mno-unaligned-access` 61,280 bytes (416 more); `--param=inline-unit-growth=20` 60,864 bytes with an identical runtime image; and `--param=max-inline-insns-auto=8` 60,864 bytes with different code. Static RAM and maximum individual stack frame were unchanged. None was adopted. A serializer helper IPA/byte-copy experiment produced no verified smaller load span and was not adopted. Reducing the RMS square-root loop's start bit removed iterations but left the image span unchanged; that speed-only change was also discarded for this footprint task.

## Accepted: fixed-width clipping history

The FIR stage previously set/cleared a cursor-dependent bit in a 64-bit clipping bitmap. Only the presence of a clipped sample within the last 63 stage inputs is externally observed. The bitmap now shifts in the newest flag at bit zero and masks away flags older than 63 inputs. This preserves the exact window, reset behavior, warmup, output cadence and two-stage propagation while removing variable-width 64-bit shift/mask code. A compile-time assertion requires a nonzero history shorter than 64 bits. Samples and filter coefficients are unchanged.

A clean configured Release build occupies 60,824 bytes, ending at 0x0800ED98: 40 bytes less than cab7e80 and 2,664 bytes free in the 63,488-byte application region. Initialized data remains 112 bytes, BSS 16,204 and stack reservation/alignment 2,052, leaving 2,112 bytes unallocated. The stage prologue and its stack allocation are unchanged. Fresh decimator, spectral/review and filtered-transition suites passed; an independent original-order convolution oracle matched 78,660 input calls across 40 streams, including clipping, reset and invalid-input cases.

This latest source change has not been flashed. The running-board observations above apply specifically to cab7e80, whose application headroom is 2,624 bytes. The current source-build headroom is 2,664 bytes. No probe debugging or additional programming was performed during this size-only experiment.
