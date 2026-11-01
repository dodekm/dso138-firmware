# Changelog

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
