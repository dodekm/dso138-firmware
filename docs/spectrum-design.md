# Spectrum analyzer engineering record

Development started 2026-11-02. This is an active design and verification record; the spectrum user interface is not yet available in the deployed firmware.

## Scope revision

The user requested basic FFT functionality and prioritized Flash use, then requested lightweight peak detection. The implemented view is a fixed 512-point, mean-removed, periodic-Hann spectrum with a dBFS trace, actual frequency span, bin spacing and dominant raw-bin frequency. FFT clipping warnings were explicitly removed; time-domain clipping handling remains unchanged. The user also authorized removing the six 10-500 s/div rolling timebases and their oversampling/FIR backend. Advanced FFT controls, interpolation, calibrated spectral volts/RMS, linear magnitude, zoom and persistent FFT preferences are outside this revision. Other time-domain features, measurements and persistence remain required.

## Requirements and budgets

Preserve the remaining time-domain controls, triggering, fast acquisition, measurements, calibration, supported saved records and rendering. Implement an independently derived C++ FFT; no existing FFT implementation is a production dependency. No dynamic allocation, runtime trigonometry, RTOS, Cortex-M4 DSP instructions or hardware floating-point assumptions are introduced.

The starting configured image occupied 60,504 bytes, leaving 3,000 bytes before storage at 0x0800F800. The current integrated image occupies 59,440 bytes and leaves 4,048 bytes. SRAM comprises 112 initialized bytes, 14,468 BSS bytes and 2,052 reserved stack/alignment bytes, leaving 3,848 unallocated bytes. Fresh CubeIDE Release and Debug builds reproduce this footprint. The minimum stack remains 2,048 bytes; alignment padding is not treated as stack capacity.

The current 500 ksample/s cap, ADC settings, timers, DMA and interrupt ownership remain unchanged. Hardware sampling continues independently within an acquisition record. The existing instrument intentionally stops after a completed record and rearms only after processing and presentation; it is not a continuous recorder. Spectrum processing belongs in that same stopped-producer interval. Spectrum RUN must still acquire repeated records, and HOLD must preserve a record for analysis.

## Research and measurement conventions

Manufacturer FFT functions inform the design without supplying implementation code. Useful features include explicit amplitude references, dominant-frequency readout and clear frequency span. Leakage suppression, close-tone resolution and coherent amplitude accuracy are different concerns. See Tektronix's [FFT/window discussion](https://www.tek.com/vn/documents/application-note/fundamentals-mdo4000-series-mixed-domain-oscilloscope), appendix A; Keysight's [1200 X-Series guide](https://www.keysight.com/bo/en/assets/9018-70020/user-manuals/9018-70020.pdf), FFT section; Rigol's [DS2000A guide](https://eu.rigol.com/eu/Images/MSODS2000AUserGuideEN_tcm30-2897.pdf), chapter 6; and Siglent's [SDS5000X guide](https://siglentna.com/wp-content/uploads/dlm_uploads/2019/01/SDS5000X_UserManual_UM0105X-E01C.pdf), FFT section. Vendor redirects can vary; the retired Tektronix record-length FAQ returned HTTP 410 and is not relied on.

The [JYE Tech specification](https://jyetech.com/dso-138-oscilloscope-diy-kit/) gives a nominal 200 kHz analog bandwidth, 12-bit ADC and 1,024-point record. Its advertised 1 Msample/s ceiling is not the sampling rate selected by this firmware. Neither analog bandwidth nor Nyquist frequency describes a guaranteed flat, calibrated passband or a brick-wall anti-alias filter.

The [Cortex-M3 programming manual](https://www.st.com/resource/en/programming_manual/cd00228163-stm32f10xxx-20xxx-21xxx-l1xxxx-cortex-m3-programming-manual-stmicroelectronics.pdf), PM0056, documents scalar multiply/long multiply and integer instructions. The implementation targets Cortex-M3 Thumb-2 with software floating-point ABI. Timing and instruction-level optimization require actual Cortex-M3 evidence; desktop execution times cannot establish target throughput.

For the actual frozen sampling rate `Fs = timer_hz / divider`, bin spacing is `Fs/N`, Nyquist is `Fs/2`, and observation duration is `N/Fs`. Zooming the display changes none of those quantities. Labels must follow the frozen record, including recalled records, rather than current switches or nominal timebase labels.

The forward transform convention is `X[k] = sum(x[n] w[n] exp(-j 2 pi k n/N))`. The custom engine returns `R[k] = X[k]/N` in the input's fixed-point units. With coherent gain `CG = sum(w)/N`, an interior one-sided bin has peak amplitude `2 abs(R[k])/CG`; DC and Nyquist use `abs(R[k])/CG` without doubling. Sinusoidal RMS is peak divided by sqrt(2); DC and sampled Nyquist need their own interpretation. A bin amplitude is not the total waveform RMS.

Rectangular and periodic Hann windows were compared; the basic view selects periodic Hann. Rectangular has CG=1, ENBW=1 bin and a two-bin null-to-null main lobe. Periodic Hann has CG=1/2, ENBW=1.5 bins and a four-bin main lobe. These follow from the window sums, not a library's normalization. Quantized coefficients must preserve or explicitly correct the actual coherent gain. Subtracting the unweighted record mean before Hann windowing need not make the weighted DC bin exactly zero; DC is excluded from the dominant-tone search.

The dBFS reference is a 2,048-code peak sinusoid at the ADC: `20 log10(A_peak/2048)`. The basic FFT view does not report calibrated spectral volts or RMS; existing time-domain voltage/RMS measurements remain unchanged. No laboratory calibration or frequency-response correction is inferred. Input clipping can generate misleading harmonics, but the user requested no FFT clipping warning; check the same record in Scope for its ADC clipping status. Zero power maps to the plot floor and does not create a peak readout.

## Transform and storage architecture

The preferred candidate packs adjacent real samples into an N/2-point complex transform, then reconstructs the positive-frequency spectrum. The reusable core supports 256 and 512; the basic frontend selects 512. A shared quarter-wave table serves both. Mathematical prototypes also evaluated 1,024 and 2,048; their larger mixed-precision workspace and, for 2,048, input-record requirement are not justified by the present memory/record architecture. They must not be advertised as supported firmware lengths.

The core operates on caller-owned `int32_t` words, independently of ADC metadata or the display. With the current preparation convention each word is a centered, windowed sample with 16 fractional bits. Packed real N=512 uses exactly 512 words, or 2 KiB. Conjugate reconstruction pairs are read before either is overwritten; Nyquist is held separately in engine state. There is no second transform-output array.

Mean accumulation, sample preparation, input validation, permutation, butterflies, reconstruction and spectral projection advance in bounded foreground slices. Cancellation invalidates borrowed pointers and partial results. Invalid length, insufficient storage and out-of-domain input must fail without publishing partial output. Numeric-format selection, overflow bounds and target cycle costs are separate verification gates.

The 2 KiB workspace reuses the stopped DMA array. DMA and spectrum alternatives are distinct trivial union members with explicit placement construction of their wrapper objects. A cast between a live `uint16_t` array and `int32_t` objects would violate the C++ lifetime/aliasing contract and is not used. DMA, recall and spectrum ownership are mutually exclusive. Active acquisition and faulted/failed-stop states deny spectrum access. A Ready record remains in its separate capture storage; the controller's copied samples also remain unchanged.

The controller cancels the analyzer and restores the DMA member before restart, release, recall/storage staging, frame replacement or teardown. It may acquire the workspace in Stopped as well as Ready, permitting held and recalled records. On completion the analyzer clears its borrowed pointers and cancels its inner FFT before the controller restores the DMA union member. Mode changes do not require HOLD: ordinary Auto/Normal/Single sequencing remains available. Live rolling/filter code is deleted. Legacy processing flags remain for record provenance only; records using removed timebase indices are rejected without automatic writes and an explicit supported save may replace them.

The FFT uses a 258-byte quarter-wave Q15 table and 32-bit data with 16 fractional bits in this frontend. Pure Q15 and block-floating Q15 were evaluated: they save data width but lose weak components beside a strong carrier in the tested cases. Q31 coefficients and software float improved numerical precision beyond the selected balance at additional table/helper cost. These were comparative prototypes, not production dependencies. Input component magnitude is limited to 2^28. Quantized twiddle norm is below one; normalized butterflies plus bounded rounding keep intermediate components below 2^29, permitting proven 32-bit sums and quarter/half rounding while multiplication retains 64-bit intermediates.

The dB path scales each complex pair together before forming a 31-bit power, preserving relative precision without a separate power buffer. A 34-byte log table and Cortex-M3 CLZ support Q12 logarithmic interpolation. A 200,000-case double-precision comparison measured maximum error 0.00827 dB, including centi-dB rounding. Peak selection reuses this already-computed logarithmic rank during projection, excludes DC and handles the one-sided Nyquist endpoint. Exact rank ties select the lower bin; no interpolation, extra FFT or extra scan is introduced. The result structure stays 24 bytes.

## Presentation and controls

The existing full-screen renderer and its 280 by 176 plot, cached tiles, dirty text and bounded display transfers are reused. Spectrum columns come from acquired sample bins, never waveform pixels. All 257 one-sided bins fit the plot; adjacent mapped columns differ by one or two pixels, so a bounded join preserves each bin and keeps the trace connected. The fixed axis is +10 to -70 dBFS at 10 dB/div; 0 dBFS is plot-relative row 22. Domain/reference changes invalidate the existing background cache through its ordinary marker/reference rules.

Controls add a Domain field after the existing focus fields. SEL reaches it; + selects Spectrum and - selects Scope. OK retains its normal RUN/HOLD behavior. The domain is session state and resets to Scope on startup; saved-settings and raw-waveform formats remain unchanged. No extra FFT configuration menu is added in this basic revision.

Scope and Spectrum share focus-value/action formatting. Timebase, Trigger, Storage and Calibration selections remain visible in Spectrum, rather than permitting edits or Flash commands behind an uninformative footer. The existing trigger label carries the compact `Pk` frequency/dBFS result, or `Peak --` for zero/DC-only input. Mean removal and windowing do not modify the stored raw record.

The spectrum view must identify N, window, mean/DC policy, actual frequency span, bin spacing and amplitude reference. Dominant frequency is the center of the strongest positive-frequency bin, without interpolation. A near-bin isolated tone generally has nearest-bin quantization uncertainty up to half a bin, in addition to clock, leakage, noise and interference effects; that is not a general accuracy guarantee. The waveform view remains available with its existing scaling and measurements.

## Verification gates and roadmap

| Stage | Required evidence | Current state |
| --- | --- | --- |
| Research and feasibility | Primary documentation, hardware budgets, independent normalization and ownership design | Recorded; length/numeric alternatives compared. |
| Custom transform | Direct DFT, endpoints, full-range/random input, cancellation and overflow bounds | Current compiled 256/512 core passes; latest focused DFT comparison observes a maximum complex error of 0.294 ADC-code units on its documented vector set, not a universal error bound. |
| Workspace | Explicit lifetime/ownership, denied overlaps, unchanged held samples, failed-stop rejection | Native lease tests and controller cancellation/rearm tests pass. |
| Spectral processing | Hann gain, endpoints, dB reference/floor, peak and projection | Native coherent/off-bin/two-tone/DC/Nyquist, budget, canary and lifecycle cases pass. |
| Integration | Fresh IDE builds, RAM/stack bounds, scope/storage/calibration/control regressions | Release and Debug build from a fresh CubeIDE import; targeted native suites pass. Static callback-expanded stack bound is 1,544/2,048 bytes, including a conservative IRQ allowance. |
| Display and usability | Actual renderer/GRAM output, axes, invalidation, visible physical-control actions | Native GRAM images reviewed; Timebase and Storage/Calibration focus remain visible; spectrum Save/Recall cases pass. |
| Hardware benchmarks | Selected-probe 50 kHz SWD, DWT timing, capture/update and recovery | Pending: CubeProgrammer detects no ST-Link. No FFT image has been flashed. |
| Delivery | Guide, LaTeX/PDF manual, build instructions and limitations | Manual 1.3 has 12 A4 pages, compiled twice and render-checked. Source/configuration and software evidence recorded; hardware qualification remains pending. |

Private tests, numerical reference implementations, transient firmware and research working files remain outside the release repository. Passing compilation or a single sinusoid is insufficient for a stage to be complete. The overall spectrum extension remains active until the complete pipeline, interface and documented verification gates are satisfied.


## Current-source software verification - 2026-11-02

A final focused run uses the FFT/spectrum sources from revision `4b8afb3`, with fresh native compilation rather than earlier executables or mixed-version headers. Tests and their source/hash manifest remain private, outside the release repository.

- The reusable 256/512 engine is checked on 12 endpoint and full-range vectors, including bounded pseudorandom input, against every one-sided bin of an independent long-double DFT: 2,316 bin comparisons. Maximum observed complex error is 0.293501 ADC-code units and RMS error 0.012500 on this vector set. Every comparison must be finite and below one code; these are observed test results, not a universal error guarantee. Chunked 1/17-operation execution is exactly equal to a single-budget run, and guard words remain intact.
- The analyzer is checked with constant DC, one-code alternation, low-level noise, 1/2/4/16-code tones, full ADC alternation and a near-full-scale tone. Peak bin and level are compared with an independent periodic-Hann DFT. All 280 projection columns must be valid and stay within rows 0-175; this check establishes bounds, not a pixel-by-pixel DFT rendering comparison. Full-scale patterns report approximately 0 dBFS; zero input has no peak.
- The current controller, acquisition and GRAM/display fixture completes 200 repeated Single-mode acquire/analyze/render/rearm cycles with Scope/Spectrum transitions, finite test deadlines, successful new records and no controller fault. A separate continuous RUN check completes 20 successive rendered spectra in Auto and 20 in Normal with no intervening button presses, requiring advancing acquisition epochs, completed renders and no controller fault. Sample checks across FFT processing remain unchanged. A RUN action during an active workspace lease returns the buffer before immediate producer restart. A panel-invalid update during analysis and subsequent valid-tuple restoration leave the held record usable and processing safe. This is simulated producer/display evidence, not a sustained physical acquisition test.

The completed software checks do not substitute for the remaining hardware gate. The selected ST-Link is not detected at the latest check. No FFT firmware has been programmed. Pending automatic target work is to verify the configured MCU/image identity, deploy and verify the candidate, check startup and recovery, and measure DWT processing slices plus repeated capture/display throughput at 50 kHz SWD. The onboard test output can establish functional integration; it cannot establish calibrated analog spectral accuracy or the front-end response.
