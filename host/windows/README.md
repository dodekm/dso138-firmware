# DSO138 Studio for Windows

DSO138 Studio is a Windows oscilloscope workspace for the DSO138 over
USB CDC. It displays current instrument settings, records live waveform
updates, and provides time-domain measurements, cursors, FFT analysis,
capture history and export. The built-in demonstration trace works
without a connected instrument.

[Download the portable Windows x64 app](https://github.com/dodekm/dso138-firmware/releases/latest).

## Quick start

1. Extract the Windows x64 package to a folder where you can write files. Keep the Qt DLLs, runtime DLLs, and `platforms` plugin folder beside `DSO138Studio.exe`.
2. Start `DSO138Studio.exe`. The demonstration waveform appears immediately.
3. To connect an instrument, select its COM port and choose **Connect**. The port must expose the DSO waveform protocol v1 over USB CDC/serial.
4. Put the instrument in **HOLD** from the front panel or, on supporting firmware, use Studio's **Put scope in HOLD** action. In the instrument settings panel, choose the timebase and trigger mode or apply an edge, level and position. Coupling and sensitivity come from the physical switches.
5. Choose **Download held**, **Single**, **Repeat…** or **Start live**. Live updates use successive records and display their update rate. Select Single mode to acquire one triggered record. Use **Freeze display** to inspect a waveform while live captures continue in the bounded history.
6. Inspect time-domain measurements or open the spectrum view. Use **File → Save capture** for a lossless `.dso138` record or **File → Export CSV** for samples, timing, calibration and capture metadata. Studio also opens native files saved by version 1.

**Current instrument settings** show the controller's acknowledged
values. **Capture settings** stay with the waveform that produced them,
even when the instrument changes. Press a physical button to take over
from the remote settings. Closing the connection or leaving it idle
restores the prior local settings. Resume local RUN from the panel.

In Studio 2.0.2, the capture list stays fixed while its menu is open.
New records are added when the menu closes. Selecting an older capture
freezes it for inspection, measurement and export.

After **Stop live**, **Download held** can wait up to one second for a
scope in HOLD to finish its foreground work. Studio reports when the
instrument has no exportable record. Captures already received remain
available in the PC history.

Scroll over the time plot to zoom or drag to pan. Press **F** to fit and
**C** to toggle cursors while the plot has focus. Drag A and B along the
trace or use arrow keys for A and Shift with arrow keys for B. The app
runs with standard user permissions.

## What the measurements mean

Raw 12-bit ADC codes remain the source of record. The app calculates
sample time from `divider / timerHz` and `pretrigger`. It uses the
capture's calibration to report volts where available and ADC codes
otherwise. The measurement panel includes peak-to-peak, minimum,
maximum, mean, total RMS, AC RMS, frequency, period and duty cycle.
Clipping and insufficient or unstable crossings affect the validity of
periodic measurements. Nominal calibration reflects the firmware's
configured conversion.

The spectrum is a single-sided peak-amplitude view of the selected
capture. Choose a window and whether to remove the mean. Frequency-bin
spacing depends on the record's actual sample rate and FFT length.
Padding to the next power of two refines the plotted grid. The
information available for resolving nearby tones still depends on the
record duration and window. Peaks and measurements refer to one
complete record. Live captures have gaps between records and remain
separate in history and export.

## Instrument and firmware requirements

The ordinary DSO138 firmware profile keeps the USB waveform interface disabled. Connecting this app therefore requires an instrument image that explicitly implements the documented waveform protocol v1 and exposes it as a Windows COM port. A USB cable alone does not add that firmware feature or a USB serial driver. This desktop application is only the host side; it cannot enable or configure the instrument interface.

Legacy firmware such as R6 still supports held downloads, Single and
Repeat. Studio detects whether the device supports current settings,
remote writes and configured-trigger live acquisition. See
[Verification](VERIFICATION.md) for evidence and current limitations.

## Build and package

Use 64-bit Windows, CMake 4.1.1, Ninja, Qt 6.8.3 `msvc2022_64`, and the matching Visual Studio 2026 x64 C++ toolset 14.44. Open an x64 Visual Studio developer command prompt using toolset 14.44, then run PowerShell from that prompt:

```powershell
$QtRoot = Join-Path $env:LOCALAPPDATA 'DSO138-Desktop-Tools\Qt\6.8.3\msvc2022_64'
$CMake = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$Ninja = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
& $CMake --preset windows-release "-DCMAKE_PREFIX_PATH=$QtRoot" "-DCMAKE_MAKE_PROGRAM=$Ninja"
& $CMake --build --preset windows-release --parallel
```

The packaging helper performs a separate clean build and stage in a unique temporary directory, installs the app at the package root, deploys Qt runtime dependencies discovered by `windeployqt`, and copies only the x64 VC143 runtime DLLs actually imported by the deployed app and Qt DLLs. It creates a portable ZIP and SHA-256 sidecar under `host\windows\dist` by default. It refuses to overwrite an existing package or checksum; use another output directory for a new artifact.

```powershell
.\package.ps1
.\package.ps1 -OutputDirectory 'D:\Releases\DSO138Studio'
```

To inspect the configured paths before packaging, pass `-QtRoot`, `-VisualStudioRoot`, `-CMakeExe`, `-NinjaExe`, `-VcVars64`, or an explicit `-RuntimeRedistDirectory` (`x64\Microsoft.VC143.CRT`). The script keeps its staging tree for review and does not remove existing build or output directories.

## Package contents and licensing

The ZIP contains `DSO138Studio.exe` at its root, deployed Qt DLLs/plugins and app-local VC143 runtime DLLs, this README, the DSO138 Studio MIT license, GNU GPL-3.0 and LGPL-3.0 license texts, third-party notices, SPDX SBOMs for Qt Base and Qt Serial Port, and a build manifest. The `licenses\sbom` files identify the Qt build components and their license declarations. The application links to Qt dynamically; the Qt libraries and platform plugin remain replaceable shared files beside the executable.

See `licenses\THIRD_PARTY_NOTICES.md` for license and source links. The application source lives in `host/windows` in the development checkout. Qt 6.8.3 source is available from the official Qt release archives linked in the notices. Microsoft CRT redistribution follows the Visual Studio redist license and Microsoft's app-local deployment guidance.
