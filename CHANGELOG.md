# Changelog

## Unreleased — Basic FFT and compact settings/calibration storage

- Add an independently developed fixed-point real FFT and a fixed 512-point, mean-removed, periodic-Hann spectrum view. It uses the frozen record's actual timer rate, ADC peak-referenced dBFS, a fixed +10 to -70 dBFS plot and a strongest positive-frequency-bin readout without interpolation.
- Recover 176 more bytes with bounded 32-bit UI integer formatting, checked byte-offset label tables and a shared LCD fallback command loop. Existing scope, FFT and calibration behavior is retained.
- Share exact scaled-ratio arithmetic between waveform projection and frequency measurement, recovering 96 bytes of Flash with unchanged numerical results. The normal projection path remains inline.
- Add a session-only Domain selector while preserving the scope controls. Scope and Spectrum share focus/action formatting; settings and calibration storage actions remain available in either domain.
- Reuse the stopped DMA workspace through explicit C++ object-lifetime transitions. No second transform buffer, dynamic allocation, runtime trigonometry, floating-point dependency or imported FFT implementation is added.
- Update the LaTeX/PDF manual to revision 1.8 with R5 USB restart checks and current stock-trial resource figures. Revision 1.7 recorded host-requested capture results; revision 1.6 added experimental USB host instructions; revision 1.5 clarified the implemented absolute peak, peak-to-peak, total RMS, mean and frequency displays. The 59,440-byte image measurement below is historical and does not describe the current storage layout. FFT hardware timing remains unverified.
- Rebuild the fully configured USB profile in Release and Debug from baseline `64a69b2`; both images match the deployed R5 image at 64,456 bytes. Enumerated nesting models 1,508 bytes; the engineering stack budget is 1,544 of 2,048 bytes with 504 bytes margin after one extra 36-byte exception-frame allowance. This is static analysis, not a runtime watermark.
- Verify a clean-copy R6 linker candidate by dropping unused `.fini_array`/`.dtors` output (40-byte body plus 8-byte table), saving 48 bytes: 64,408 bytes with 104 bytes free, Release and Debug identical. R5 remains the deployed 64,456-byte image with 56 bytes free. `.init_array` including its constructor, local RAII cleanup and explicit fault/stop cleanup remain; the nonterminating application does not rely on process-exit global destruction. R6 remains unflashed and target work remains pending.
- Pass fresh canonical-source measurement, FFT, storage and controller software oracles, including 120 independent-reference mean/RMS reductions, 2,316 FFT-bin comparisons, 268 interrupted-save reboot cases, 200 FFT lifecycle cycles and 16,777,216 EPnR write cases. Target FFT timing/display, acquisition interference and remaining USB hardware checks are still pending.

- Reserve one 1 KiB Flash page (`0x0800FC00–0x0800FFFF`), increasing the application region to 63 KiB.
- Remove the six 10–500 s/div timebases and their slow rolling, oversampling, and FIR-filtered acquisition backend. The active catalogue now spans 10 µs/div through 5 s/div.
- Remove waveform save/recall and legacy-record migration. Persist current settings and nine-slot DC calibration only. CRC validation remains enabled. The page is not erased automatically at boot; power loss during explicit Save Config or Save Cal can lose the prior stored data.
- The current page uses at most 272 bytes (32-byte envelope, 36-byte settings record, 204-byte calibration record), leaving 752 bytes spare.
- Keep CRC validation for the compact settings/calibration records. No boot-time erase is performed; an interrupted explicit save can lose the prior stored record.
- Historical pre-removal implementation experiments tracked slow-filter clipping with a fixed-width shift register and paired symmetric FIR samples. The slow rolling/filter path has since been removed.
- Use a bounded exact Q32 square root for validated ADC moments, including fractional calibration offsets, with 32-bit root/remainder arithmetic.
- Narrow the bounded fractional frequency quotient to 32 bits while preserving the original fast path, precision and rounding.
- Specialize exact waveform-position multiplication for its bounded width and simplify adjacent-sample interpolation using proven 64-bit product bounds, retaining rounding and viewport behavior.
- Store bounded private vertical projection coordinates in 32 bits, reducing Flash, RAM and stack use while retaining 64-bit horizontal geometry and interpolation products.
- Share LCD coordinate encoding, compact UI label selection and remove redundant scratch clearing.
- The previous storage layout is not migrated and old data is not preserved. USB remains disabled in the ordinary public profile. The opt-in stock-trial R5 image was deployed for bounded USB restart/capture checks and occupies 64,456 bytes of the 64,512-byte application region; this does not qualify production electrical wiring or continuous acquisition.

## 1.0.1 — Trigger LED indication

- Add a foreground, nonblocking D3 pulse for newly accepted trigger crossings.
- Document the PA15 wiring, operating modes, and limits of the indication.
- Enable measured size optimization with `-fipa-pta` and cold startup/fault hints; retain the existing font after the packing experiment increased total image size.
- Update the LaTeX/PDF user manual to revision 1.1.
- At 1.0.1 publication, physical LED behavior had not been checked. The operator later confirmed D3 working on the reference instrument; its pulse timing remains unmeasured.

## 1.0.0 — Initial public source release

- Publish the C++17 STM32F103 DSO138 firmware source and STM32CubeIDE project.
- Include user, build, hardware compatibility and architecture documentation.
- Require each owner to configure their own MCU UID and board identity before normal startup.
- Provide source for device-specific builds; no universal prebuilt firmware image is included.
