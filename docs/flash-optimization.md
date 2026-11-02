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

## Accepted: paired symmetric FIR

The filter now pairs samples at symmetric positions of the existing 63-sample ring. A compile-time factory derives 32 runtime weights from the unchanged public 63-coefficient table. The centre coefficient is even (4,590); the last pair visits the centre sample twice and uses 2,295, reproducing the original centre contribution exactly. Static assertions enforce the fixed odd-tap shape, coefficient symmetry and even centre. No duplicated hand-maintained coefficient list, new public interface or ring-layout change is introduced.

There are 32 multiplies per stage output instead of 63. Each paired sum is widened before multiplication; the existing full-coefficient absolute-sum bound still bounds every partial accumulator, so regrouping introduces no overflow or rounding change. Rounding remains after the full integer convolution. The public table is used at compile time; the linked image retains only the derived 64-byte table.

The configured image occupies 60,800 bytes, ending at 0x0800ED80: a further 24-byte saving and 2,688 bytes of application headroom. Initialized data, BSS and stack reservation remain 112, 16,204 and 2,052 bytes, leaving 2,112 bytes unallocated. Comparing 232 compiler stack entries found no new/removed entries and one frame increase, from 20 to 28 bytes in the FIR push helper. Conservatively adding those eight bytes once to the prior stack model yields 1,764 bytes against the 2,048-byte reservation; the helper is not recursive and the two stages execute sequentially.

Fresh native decimator, review and filtered-transition suites passed. The independent original-order oracle matched 78,660 input calls across 40 streams, including adversarial/random samples, clipping, reset and invalid-input cases. All application sources were compared with the tested candidate, excluding only the per-board configuration header and normalized line endings. A 64-slot-ring alternative achieved the same image span and was discarded to retain the existing layout. Initial stale private builds were corrected before measuring; only clean final candidate artifacts and fresh tests support acceptance.

This source optimization has not been programmed or debugged on the board. Multiplication counts are algorithmic counts, not a claim of measured execution time. The flashed cab7e80 image still has 2,624 bytes free; the latest source build has 2,688 bytes free.

## Accepted: single-record opening and bounded RMS root

The semantic storage layer still carried candidate/chosen bank indices after the transport layer became single-bank. Opening now accepts or rejects its sole candidate directly. The final header reread is retained because calibration validation reuses header scratch space; source/bounds checks, unknown-format write inhibition, transport-failure handling and publication rules remain. The isolated change saved 96 bytes.

The private RMS square-root routine now consumes 28 base-four digits using a 32-bit root and remainder, replacing the generic 32-step wide-state routine. Its three callers operate on squared 12-bit code-domain Q32 values. Raw and AC mean squares are bounded by `AdcMax² × 2³²`. For fractional-zero calibration, the existing integer calculation differs from the exact squared-mean expression by less than one unit upwards; since the endpoint bound is integral, the corrected result does not exceed that bound. Compile-time bounds and comments cover this domain. Root/remainder/trial arithmetic fits 32 bits throughout; no approximation or public input check was introduced or removed. The isolated change saved 16 bytes.

The combined clean build occupies 60,688 bytes, ending at 0x0800ED10, leaving 2,800 bytes of application Flash. Initialized data, BSS and reserved stack/alignment remain 112, 16,204 and 2,052 bytes; unallocated SRAM remains 2,112 bytes. No existing common stack frame grew. The root is now an outlined 16-byte-frame helper called three times per completed record, not per sample or from an ISR. Calls are sequential, so a conservative addition to the previous stack model gives 1,780 bytes within the 2,048-byte reservation. No new hardware timing claim is made.

Fresh combined-source tests passed the 949-boundary, three-mode storage interruption oracle, storage bounds/retry cases, empty/unsupported/malformed sole-record outcomes, both storage UI suites, measurement/fractional RMS suites and filtered processing reviews. A wrapper compiled from the actual private helper matched `math.isqrt` for 1,148,593 inputs: all values below 2²⁰, 100,000 deterministic random 56-bit values and square/boundary cases. All 80 application files other than the per-board header were checked against canonical sources after integration.

The new source build has not been flashed. The running cab7e80 image remains unchanged with 2,624 bytes free. All work in this batch was offline, as requested.

An additional exact frequency-scaling experiment split the overflow fallback into two factors of 1,000, reducing its fractional multiplication loop from 20 to 10 iterations and retaining the original fast path. Big-integer comparisons covered overflow thresholds, half-step neighbours, one million bounded random ratios and 241,971 caller-valid overflow cases. Results matched, but the candidate ended at 0x0800ED28 (60,712 bytes), 24 bytes larger than the 60,688-byte baseline. It was rejected; fewer loop iterations alone did not yield a smaller image. The accepted source and compiler flags remain unchanged.

## Accepted: narrow the fractional frequency quotient

The frequency overflow fallback previously used a 64-bit accumulator for both the fractional quotient and remainder. The quotient is strictly below the fixed scale of 1,000,000: each long-multiplication prefix represents a product of a fraction below one with a prefix of that scale. It now uses 32 bits; the remainder and final whole-plus-fraction result remain 64-bit. A compile-time assertion checks the quotient-width requirement. The original multiplication-safe fast path, 20 fallback iterations and both output rounding rules remain unchanged. No loop, runtime call or stack frame was added.

A forced configured Release build occupies 60,672 bytes, ending at 0x0800ED00: a 16-byte saving and 2,816 bytes of application headroom. Initialized data, BSS and stack reservation/alignment remain 112, 16,204 and 2,052 bytes; unallocated SRAM remains 2,112 bytes. The stack-usage entries are unchanged. Native compiled measurement and fractional-RMS suites passed, including frequency sweep and half-step cases. Separately, a Python model of the loop matched arbitrary-precision results on 100,004 bounded pairs; that model does not execute the compiled C++ helper. The assertion/comment-only update produced an identical runtime binary.

Two alternatives were rejected: sample-reader sharing grew the current image by 24 bytes and its local frame by eight; a uniform two-stage frequency scaler saved 88 bytes but introduced a ten-round long-division loop on the existing short path without evidence that the runtime trade-off was acceptable. The accepted implementation retains the fast path. All application sources were compared with the tested candidate, excluding per-board configuration and line-ending normalization. No board debugging or flashing was performed; the running cab7e80 image still has 2,624 bytes free.

## Accepted: specialize exact waveform projection arithmetic

The overflow-safe position helper now takes a 32-bit multiplier: viewport width times 1,000,000 is at most 320,000,000. Its original checked-product fast path remains; the wide-product fallback processes 32 multiplier bits instead of 64, with a 32-bit fractional quotient. Each partial quotient is bounded by the corresponding multiplier prefix. The remainder and final result remain 64-bit. A compile-time check protects the multiplier-width assumption.

Adjacent-sample interpolation no longer needs the general overflow-safe helper. Valid calibration bounds each sample's vertical coordinate below 25.8 million in magnitude, giving an adjacent vertical difference below 2^26. The largest accepted divider/viewport ratio gives an adjacent horizontal gap below 2^34: divider <= UINT32_MAX, width <= 320, timer >= 1 MHz and timebase >= 10 us/div. The projector updates its previous point for every source sample, including offscreen samples. Consequently the interpolation numerator is below 2^60 and fits an ordinary 64-bit product. The horizontal distance remains 64-bit; endpoint handling and half-up rounding are retained.

The final configured optimized ELF ends at 0x0800EC88: .data load address 0x0800EC18 plus 112 bytes, or a 60,552-byte image. This saves 120 bytes and leaves 2,936 bytes in the 62 KiB application region. Initialized data, BSS and stack reservation/alignment remain 112, 16,204 and 2,052 bytes. Compiler stack reports show sample_position_x decreasing from 56 to 48 bytes and segment_y from 64 to 32; no common frame grows. A fresh independent configured Release build reproduced the same load span.

Native scaling, timebase projection, voltage math, wide-gain and UI controller suites passed. A fresh optimized native compilation additionally checked 18 timebase presets, 18,810 trigger coordinates and 15,120 ramp columns. An arbitrary-precision Python reference compared actual compiled C++ projector output for all 640 columns of two extreme viewports: maximum representable valid divider 4,294,901,760, 320-pixel width, a horizontal gap exceeding UINT32_MAX, maximum calibration gain, horizontal pan, a right-edge trigger and both vertical directions. These cases exercise visible 120/121 and 119/120 rounding transitions rather than only clipped output. Application sources match the tested candidate except private board configuration and line-ending normalization. Tests and temporary artifacts remain outside the release repository. All work was offline; the board remains on cab7e80, with 2,624 bytes free.

## Accepted: bounded private vertical coordinates

Only the projector's private vertical coordinates and helpers now use int32_t. Validated calibrated coordinates have magnitude below 25.8 million; raw-code fallback is bounded by viewport height and the signed pixel offset. Adjacent differences remain below 2^26, and rounded interior interpolation lies between its endpoint coordinates. These bounds cover the new narrowing casts. Horizontal coordinates, horizontal gaps and interpolation products/remainders retain 64-bit arithmetic. Public conversion interfaces and validation behavior are unchanged.

Configured Debug and Release builds both end at 0x0800EC48 (.data LMA 0x0800EBD8 plus 112 bytes), occupying 60,488 bytes and leaving 3,000 bytes of application Flash. The saving is 64 bytes. BSS decreases by eight bytes to 16,196; initialized RAM remains 112 and stack reservation/alignment 2,052, leaving 2,120 bytes unallocated. Across 247 matched compiler stack records there are no increases or added records: segment_y falls from 32 to 24 bytes and merge from 16 to 12; the maximum individual frame remains 384 bytes.

Because this changes class layout, native verification rebuilt every production and test translation unit used by the targeted suites against the final headers, with optimization and assertions enabled; no old objects were reused. Scaling, timebase projection, voltage math, wide-gain and UI controller integration passed. The freshly compiled extreme-X fixture again matched the arbitrary-precision reference for all 640 columns. A broader historical host-test compilation contains obsolete storage interfaces and target-only build assumptions and is not counted as passing. Full application sources were compared with the tested configured candidate, excluding only private board settings and line endings. No board access occurred.

A separate paired-FIR experiment narrowed its 32-iteration loop counter from 16 to eight bits; the configured image span was unchanged at the then-current 60,552-byte baseline, so it was discarded.

## Rejected: UI phrase compression

A bounded experiment extracted 179 distinct UI literals and accounted for suffix pooling. The original estimated pool was 1,796 bytes; 20 one-byte phrase tokens reduced encoded strings to 1,438 bytes, with 133 bytes of phrase data and an initial 40-byte offset table. The resulting 185-byte data estimate was not a whole-image saving. An initial target experiment saved 120 bytes but added formatter/helper calls and stack use.

A final readable, guarded variant used byte offsets, compile-time bounds, and 54 individual assertions that each encoded literal decoded to its readable original. A forced configured build on the accepted vertical-coordinate source occupied 60,392 bytes: 96 fewer than the 60,488-byte baseline. Text grew by 104 bytes and read-only data shrank by 200. There were no additional glyph-level calls in that variant, but Text::add grew from 100 to 224 bytes and its frame from 20 to 32 bytes. Plain input acquired two token-detection instructions per byte, and token expansion added guarded lookup/loop work. It also duplicated the shared text-range update inside the formatter to avoid another outlined helper. No target timing was measured.

Fresh portable integration and numeric tests passed, and the literal assertions checked the encoded messages. The historical calibration test's demand for fallback from a malformed generation-two record to generation one is obsolete under the authorized single-bank policy; it is not counted as a passing test. The experiment is rejected: its modest net saving does not justify extra formatting work, increased stack consumption and duplicated range logic without stronger timing evidence. Production labels and formatter remain unchanged. All experimental source, scripts and outputs stay outside the release repository.

## Final software optimization assessment

The accepted configured image occupies 60,488 bytes and leaves 3,000 bytes for future application code. Relative to the initial 61,280-byte image with 160 bytes free, firmware reduction contributes 792 bytes and the authorized storage reduction contributes 2,048, giving 2,840 additional bytes of headroom. This is the smallest accepted image in this investigation, not a proof of a global mathematical minimum.

| Objective area | Evidence and disposition |
| --- | --- |
| Quantitative baseline and physical occupancy | Configured ELF/map, size, nm, objdump and readelf inspection; load span includes initialized RAM's Flash image and alignment, not just the size tool's text column. The final address is 0x0800EC48, below storage at 0x0800F800. |
| Source duplication, state and abstraction | Measured storage, label-selection and LCD encoding consolidation; LTO already merges identical CRC implementations. Existing work budgets, ownership, validation and fault behavior are retained. |
| Constants and numerical specialization | Measured coefficient pairing, bounded RMS/frequency/projection arithmetic and vertical state. Font and string packing alternatives were tested and rejected where total size or engineering costs did not justify them. |
| Compiler and linker | Os/Oz, LTO, section GC, IPA, outlining, switch lowering, linker relaxation/layout and inline parameters were examined as documented above. Required per-file Capture settings remain. No unmeasured flag change was retained. |
| Toolchain alternatives | Official GNU Arm 13.3 produced a larger image; installed GNU 14.3 remains selected. A newer official package could not be obtained through its public download routes and was not substituted with an unverified package. |
| C++ and library runtime | Current linked-symbol inspection finds no allocator, new/delete, printf, RTTI/vtables or exception runtime. The small memory primitives and required static initialization remain; constructor lifecycle semantics were not weakened. |
| HAL, target and assembly | Unused HAL sections are discarded. Required peripheral guards and diagnostics remain. Thumb-2/Cortex-M3 software-float configuration is retained. The already optimized 64-bit division helper remains for its real callers. |
| RAM and stack | Final data/BSS/reserved stack are 112/16,196/2,052 bytes; heap remains zero. Full matched stack records show no increase for the accepted vertical change. Earlier callback-expanded conservative stack accounting remains applicable because these changes reduce frames and add no call edges. |
| Behavioral and performance constraints | Fresh target builds and relevant native arithmetic, projection, UI and persistence checks are documented with their scope. Unchanged sources were compared with configured candidates. Accepted arithmetic narrowing preserves exact results; inspected segment/merge instruction streams shrink and add no call sites. Sampling clocks, DMA and interrupt configurations are unchanged. |
| Scope limits | No new physical timing, acquisition or display qualification is claimed. The board remains on cab7e80. Hardware debugging and flashing were excluded by the user's latest instruction. Obsolete historical tests are explicitly excluded rather than reported green. |

Final comparison matched all 158 release source/dependency files under App, Inc, Src, Drivers and Startup against the tested configured tree after line-ending normalization, excluding only the deliberately private board-identity header. The repository passes diff whitespace checks and contains none of the private test or experimental files.

The investigation covered the major linked contributors and the specific smaller opportunities supported by source/assembly evidence. Remaining experiments that trade runtime work or additional maintenance complexity for small savings were not accepted merely to lower a size number. This closes the software footprint investigation at the current verified configuration; different features, performance trade-offs or future toolchains require a new comparison.

## Deployment — 2026-11-02

The accepted 60,488-byte configured ELF was programmed through the owner-selected ST-Link at 50 kHz SWD after matching the MCU identity and 64 KiB device capacity. All 158 release source/dependency files matched the tested configured tree, apart from the private board-identity header. CubeProgrammer erased application pages 0–59 and reported successful download verification; the final 2 KiB storage pages were left untouched. No Flash backup was taken, as requested.

After a separate software reset/run, non-resetting HOTPLUG reads confirmed startup stage 6, LCD ID 0x9341, running application phase and zero startup/power/display/application fault fields. The observed acquisition publication contained no captured frames or qualified physical generation, so this limited startup check is not an acquisition, waveform or performance qualification. Normal-mode reconnects disturbed startup and were replaced by HOTPLUG observation. Earlier statements about the board remaining on cab7e80 describe the preceding offline investigation.

## Basic FFT and user-authorized slow-backend removal — 2026-11-02

The user narrowed the spectrum extension to basic functionality, removed its clipping warnings, authorized removing the 10-500 s/div rolling/filter backend, and then requested a compact peak detector. The current source retains the other scope features and saved-record format. The custom real FFT uses integer arithmetic and the stopped DMA workspace, with no imported FFT implementation, dynamic allocation or runtime trigonometry.

The following are forced configured whole-image measurements with the same GNU Arm 14.3 Release profile, board identity, 62 KiB application region and 2 KiB storage reservation. Load spans include initialized RAM's Flash image and alignment.

| Build step | Load span | Application headroom | Change |
| --- | ---: | ---: | ---: |
| Basic FFT before removing slow rolling/filtering | 65,120 B | 1,632 B over limit | Unflashable analysis build |
| Slow backend and six timebases removed | 59,544 B | 3,944 B | 5,576 B Flash saved |
| Compact dominant-bin detector and readout | 59,824 B | 3,664 B | 280 B added |
| Shared Scope/Spectrum focus and action formatting | 59,712 B | 3,776 B | 112 B recovered |

Removal alone reduced BSS from 16,316 to 14,460 bytes, saving 1,856 RAM bytes. Peak ranking adds eight bytes, leaving final BSS at 14,468. Initialized data remains 112 bytes and heap remains zero. With 2,052 bytes of reserved stack/alignment, 3,848 bytes of SRAM remain unallocated. Relative to the pre-FFT 60,488-byte image, the current image is 776 bytes smaller while including basic FFT and peak reporting.

The numeric path was reduced structurally: complex pairs are scaled together to form 31-bit powers, CLZ and a 34-byte logarithm table replace iterative wide-power processing, and bins are projected directly without a stored power array. The peak scan reuses that already-calculated logarithmic rank, excludes DC, accounts for the Nyquist endpoint and chooses the lower bin on an exact rank tie. It adds no transform, pass, coefficient table or result-buffer allocation; the result structure remains 24 bytes. There is no sub-bin interpolation or claim of extra resolution. Quantized-Hann coherent gain and one-sided peak normalization are documented in the spectrum engineering record.

Shared focus formatting also fixes a usability problem: Spectrum now shows the actual selected Timebase, Trigger, Storage or Calibration field and action instead of allowing hidden edits behind a generic footer. Native controller and storage tests cover Spectrum Save/Recall, HOLD, cancellation/rearm and unchanged raw samples. Preset tests prove the original first 18 timer pairs remain unchanged and reject indices 18-255 without changing the supplied output. Older records using removed indices are rejected without automatic Flash writes; an explicit supported save can replace them.

Fresh STM32CubeIDE imports build Release and Debug with all 39 current C++ sources discovered, the new FFT/spectrum modules included, the deleted rolling/decimator modules absent, and the non-LTO Capture override retained. Both reproduce the 59,712-byte load span, ending at 0x0800E940. Application C++ compilation retains strict warnings as errors; generated make peer-target warnings concern the LTO .su/.cyclo sidecars.

An exact-image call-graph rebuild adds analysis metadata but produces the identical 59,712-byte runtime binary (SHA-256 8DBEDCF006713668EB3D785EBCFE0EEEFE251E9AD10D83B65ED47CE7F804CCBD). Its maximum callback-expanded foreground path is 1,348 bytes. All 46 reachable indirect sites are bound to the configured callbacks, with no unresolved reachable sites or recursion. A conservative 212-byte IRQ/software/hardware-frame allowance gives a 1,560-byte bound. Against the actual 2,048-byte minimum stack this leaves 488 bytes; the four alignment bytes are not usable stack.

The LaTeX/PDF manual is revision 1.3, with 12 rendered A4 pages reviewed. Simulated images come from the real controller, renderer and LCD/GRAM command path with synthetic inputs; they are not physical captures. No FFT image has been flashed or timed. CubeProgrammer currently reports no ST-Link, so hardware qualification remains pending. Earlier physical observations in this report apply to their named older images.


## Shared exact ratio — 2026-11-02

Waveform horizontal positioning and frequency measurement now share the exact overflow-safe multiplication/division by 1,000,000. Factoring the viewport width into the unscaled numerator permits one 20-step wide-remainder implementation instead of separate 32- and 20-step implementations. Valid capture/pan/width bounds keep the unscaled projection product within 64 bits, including maximum-divider metadata. The ordinary product-safe path stays inline; only the wide fallback is shared out of line. Frequency retains separate rounding from the unrounded microhertz quotient to avoid double rounding at millihertz half-steps. No feature, input range or validation was removed.

Forced configured Release and CubeIDE Release/Debug builds reduce the load span from **59,712 to 59,616 bytes**, ending at **0x0800E8E0**: **96 bytes saved**, leaving **3,872 bytes** before the storage region. Initialized data/BSS and SRAM headroom remain 112/14,468/3,848 bytes. The runtime binary SHA-256 is `C786D60334D1E98B35B6A500FBA6475AC87B7A282707EA039B40505C3FFEDECF`.

Native checks pass all 18 timer plans, 18,810 trigger coordinates, 15,120 ramp columns, voltage arithmetic, measurement accuracy/validity/extremes and rounding ties. Another 20,025 direct quotient/remainder cases match Python arbitrary-precision integer results, including product-overflow boundaries. Extreme-divider projection outputs also match the earlier implementation and signed-floor oracle. These are software checks, not measured target speed improvements.

The common projection frame remains 48 bytes; the rare fallback adds a 32-byte nested helper frame. An exact-binary call-graph audit includes that new edge and still bounds foreground at 1,348 bytes and foreground plus conservative IRQ allowance at 1,560 bytes. All 46 reachable indirect sites are mapped; no unresolved calls or recursion remain. The minimum usable stack remains 2,048 bytes, giving 488 bytes of static margin.

Rejected alternatives remain outside the repository: moving all ratio work out of line saved 152 bytes but added a call per projected point; paired twiddle fetching grew the image by eight bytes; stage-batched FFT execution grew it by 32 bytes. Legacy filtered-record metadata remains necessary: a held filtered capture can legitimately be saved after selecting a supported viewport timebase. Removing the live slow acquisition backend does not make those saved records invalid.
