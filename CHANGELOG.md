# Changelog

## Unreleased — Basic FFT, Flash headroom and single-bank storage

- Add an independently developed fixed-point real FFT and a fixed 512-point, mean-removed, periodic-Hann spectrum view. It uses the frozen record's actual timer rate, ADC peak-referenced dBFS, a fixed +10 to -70 dBFS plot and a strongest positive-frequency-bin readout without interpolation.
- Add a session-only Domain selector while preserving the scope controls and raw waveform/settings storage format. Scope and Spectrum share focus/action formatting; Save, Recall and Calibration remain visible in either domain.
- Reuse the stopped DMA workspace through explicit C++ object-lifetime transitions. No second transform buffer, dynamic allocation, runtime trigonometry, floating-point dependency or imported FFT implementation is added.
- Update the LaTeX/PDF manual to revision 1.3. The configured software image occupies 59,712 bytes, leaving 3,776 bytes free. New FFT hardware timing and deployment remain unverified.

- Reserve 2 KiB instead of 4 KiB for saved records, increasing the application region to 62 KiB.
- Remove the six 10–500 s/div timebases and their slow rolling, oversampling, and FIR-filtered acquisition backend. The active catalogue now spans 10 µs/div through 5 s/div. Saved records with removed timebase indices are rejected rather than mapped onto a different setting; users can explicitly save a supported replacement.
- Replace copy-on-write saving with a single bank. Power loss during erase/programming can lose the previous saved waveform, settings and calibration; checksums and commit validation still reject incomplete records.
- Stage the complete payload in bounded RAM before erasing, preserving existing waveform data when saving only settings or calibration.
- Simplify storage to one record with no mutable bank address/selector, and avoid clearing staged bytes that are completely overwritten before use.
- Remove remaining multi-candidate bookkeeping from semantic storage opening while retaining final-header validation and malformed/unknown-format handling.
- Historical pre-removal implementation experiments tracked slow-filter clipping with a fixed-width shift register and paired symmetric FIR samples. The slow rolling/filter path has since been removed.
- Use a bounded exact Q32 square root for validated ADC moments, including fractional calibration offsets, with 32-bit root/remainder arithmetic.
- Narrow the bounded fractional frequency quotient to 32 bits while preserving the original fast path, precision and rounding.
- Specialize exact waveform-position multiplication for its bounded width and simplify adjacent-sample interpolation using proven 64-bit product bounds, retaining rounding and viewport behavior.
- Store bounded private vertical projection coordinates in 32 bits, reducing Flash, RAM and stack use while retaining 64-bit horizontal geometry and interpolation products.
- Share LCD coordinate encoding, compact UI label selection and remove redundant scratch clearing.
- The old lower storage bank becomes application space; existing saved records are not guaranteed to migrate. No new hardware deployment is claimed.

## 1.0.1 — Trigger LED indication

- Add a foreground, nonblocking D3 pulse for newly accepted trigger crossings.
- Document the PA15 wiring, operating modes, and limits of the indication.
- Enable measured size optimization with `-fipa-pta` and cold startup/fault hints; retain the existing font after the packing experiment increased total image size.
- Update the LaTeX/PDF user manual to revision 1.1.
- Hardware has not been flashed or tested for this revision; the LED is an activity cue, not a timing measurement.

## 1.0.0 — Initial public source release

- Publish the C++17 STM32F103 DSO138 firmware source and STM32CubeIDE project.
- Include user, build, hardware compatibility and architecture documentation.
- Require each owner to configure their own MCU UID and board identity before normal startup.
- Provide source for device-specific builds; no universal prebuilt firmware image is included.
