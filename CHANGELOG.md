# Changelog

## Unreleased

- Refactor firmware and Studio internals into focused modules with shared protocol and instrument contracts.
- Fix standalone storage initialization by validating the configured full MCU UID and Flash size without relying on the software-inaccessible debug ID register.
- Verify settings and calibration save/restore on the reference board, including standalone startup and cleanup of a synthetic test profile. See [verification results](docs/release-readiness.md#storage-identity-and-persistence-follow-up).

## Studio 2.0.2

- Keep capture history rows stable while the selection menu is open and catch up after it closes.
- Recover the USB session when a malformed acknowledgement leaves capture or frame ownership uncertain.
- Wait briefly for the held record when a download follows Stop live.

## Studio 2.0.1

- Keep capture buttons available during background settings reads and run the first pending action after readback.
- Keep unapplied trigger edits available when a settings write is rejected.
- Reduce redraw cost for waveforms and spectra, and display live timing separately from action messages.

## Studio 2.0 and companion firmware

- Add live instrument readback and remote timebase and trigger settings.
- Add successive-capture live view with measured delivery intervals, frozen capture metadata, and bounded history.
- Add PC-side measurements and windowed FFT analysis. Retain native capture and CSV export.
- Improve host capture delivery by reusing the completed LCD scene in HOLD. The configured USB firmware leaves 16 bytes of application Flash.

## 2.0.0 — Spectrum and USB waveform export

- Add a fixed 512-point real FFT with a periodic Hann window, dBFS display and dominant-bin readout. Spectrum reuses the stopped acquisition workspace.
- Add opt-in USB CDC raw waveform export and host-requested Single captures. Export leases preserve raw ADC samples, timer settings and frozen calibration. The ordinary build keeps USB disabled.
- Retain 18 triggered timebases (10 µs/div through 5 s/div), Auto/Normal/Single triggering, D3 indication and the remaining time-domain measurements.
- Remove waveform save/recall, the 10–500 s/div rolling and filtered timebases, minimum/maximum voltage readouts and AC RMS. Settings and DC calibration use one CRC-checked 1 KiB page. See the [upgrade notes](docs/getting-started.md#upgrade-notes) for incompatible saved records.
- Fit the fully enabled R6 profile in 64,408 bytes with 104 bytes of application headroom. Clean Release and Debug images match. The R6 linker omits unused global finalization arrays and retains startup constructors and explicit resource cleanup.
- Verify target FFT, raw-record preservation, USB restarts and session recovery, repeated captures and short acquisition tests under USB traffic. See [verification and limits](docs/release-readiness.md).
- Publish manual 2.0 as LaTeX source and a rendered PDF. The CubeIDE project builds directly and the `.ioc` serves as a partial hardware reference.

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
