# Changelog

## Unreleased — Flash headroom and single-bank storage

- Reserve 2 KiB instead of 4 KiB for saved records, increasing the application region to 62 KiB.
- Replace copy-on-write saving with a single bank. Power loss during erase/programming can lose the previous saved waveform, settings and calibration; checksums and commit validation still reject incomplete records.
- Stage the complete payload in bounded RAM before erasing, preserving existing waveform data when saving only settings or calibration.
- Simplify storage to one record with no mutable bank address/selector, and avoid clearing staged bytes that are completely overwritten before use.
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
