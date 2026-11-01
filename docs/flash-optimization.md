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
