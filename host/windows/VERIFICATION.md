# DSO138 Studio verification

## Studio 2.0

| Area | Evidence |
| --- | --- |
| Windows build | Qt 6.8.3, MSVC toolset 14.44 and CMake 4.1.1 built the integrated Release app. |
| Settings transport | Simulated supported and legacy devices exercised discovery, idle polling, acknowledged settings, dropped-acknowledgement retry, stale refresh and legacy fallback. A separate Qt test covered trigger and HOLD writes and rejected a queued write after an intervening local revision. |
| Live capture | The Qt session fixture completed three selected-mode records with settings readback, frozen metadata and a measured host interval. Separate transport tests verified that an explicit startup Busy allows one settings-checked retry, persistent Busy stops the operation, and cancellation invalidates the delayed retry. |
| Live workspace | An offscreen MainWindow test pinned the selected capture and measurements through 105 incoming frames while history stayed capped at 100. The FFT window control recomputed its summary, CSV export used the selected capture's samples and metadata, and the next frame updated the selection after unfreezing. |
| Firmware integration | Native fixtures ran the firmware protocol with simulated circular DMA. They verified 1,024 raw codes, legacy export and lease replay, timeout and stale tokens, settings writes, configured-mode capture, extended frozen metadata, stopping local RUN, and dropping a stalled acknowledgement when the session expired. |
| Standalone firmware | A native fixture started Auto acquisition with a debounced OK event, drove simulated DMA through a completed capture, and checked pixel writes to a fake ILI9341. A second OK stopped acquisition in HOLD, and Plus changed the local timebase. The final build also passed tests that held one-shots skip the duplicate redraw and that local rearm, WAIT INPUT and WAIT BACK still request a redraw. |
| Measurements and spectrum | Generated sine, DC, pulse, square, clipping and uncalibrated records exercised measurement validity, calibration and FFT windows. Checks also covered unstable periods, Nyquist, zero padding and empty input. |
| Files | Native format v2 round-tripped raw samples and full metadata, v1 imported with unavailable extension fields, and CSV preserved the trigger settings and host interval. |
| Native Windows | Waveform, measurement, and spectrum views rendered with the Windows Qt backend at effective 100%, 150% and 200% scaling. A fresh ZIP extraction launched with a Windows-only PATH, verified 19 manifest entries, and loaded all ten bundled DLLs locally. |
| Legacy hardware | The portable Studio 2 application connected to the installed R6 over USB, detected legacy firmware and disconnected. The R6 also answered a bounded status load. |
| Studio 2 hardware | The reference board accepted remote HOLD, timebase, mode and trigger writes. It exported complete raw captures at three timebases, returned the same raw record through the legacy export, and restored pre-session settings on timeout. See the bench results below. |

The configured USB-enabled firmware uses 64,496 of 64,512 application
bytes. The settings and calibration page and 2,048-byte stack
reservation are preserved. See the [engineering plan](../../docs/studio-2-design.md)
and the [USB protocol](../../docs/usb-protocol.md).

The portable ZIP is 11,121,129 bytes. SHA-256
`3242c6bd984738fc4184506dce4fda7ab186a1324c71c0a2dcc2c9aaa6005404`.
An independent clean Windows installation was unavailable.

### Reference-board acceptance

Run this check after the owner authorizes deployment. Verify the MCU
identity and 64 KiB Flash size through the selected ST-Link at 50 kHz
SWD. Verify the candidate checksum
against its sidecar and check that its programmed range ends before
`0x0800FC00`. Use application-page programming with verification, then
perform a software reset so the MCU boots through its normal reset path.
Keep the storage page and option bytes protected.

1. Connect the portable app and confirm that current settings populate
   within one second. Pause local Auto or Normal RUN if needed. Select a
   timebase and trigger mode, then set trigger edge, level, and position.
   Confirm that the displayed applied settings agree with the device
   acknowledgement. Keep an earlier capture selected and verify its
   timebase and threshold stay frozen.
2. Use the onboard test signal with Auto trigger. Record several frames
   at 100 µs/div, 1 ms/div, and 100 ms/div. Compare requested and captured
   settings, trigger index, timer-derived sample rate, and raw count.
   Record request-to-complete time, leased transfer time and payload
   bytes per second separately. In Studio, record the displayed update
   rate and the host-observed interval between delivered records. The
   design target at 1 ms/div is four displayed records per second. Host
   intervals include presentation and transfer time.
3. During a bounded fast-timebase RUN check, poll USB status and inspect
   firmware acquisition telemetry from the matching ELF. Compare the
   maximum observed poll gap with the active DMA budget and verify the
   acquisition, transport, panel, and controller fault fields. Keep the
   RUN observation distinct from the stopped-producer transfer check.
4. Stop live view, read the same held record with legacy and extended
   commands, and compare the raw samples and shared metadata. Disconnect
   or allow the remote settings to expire, then verify the pre-session
   selections. Exercise the protocol v1 held and Single capture paths.

#### Reference-board results

The final 64,496-byte build was programmed and verified on pages 0–62
and started by software reset. Its application image SHA-256 is
`c3690fe59b49ab6adc55ad6c37dca606f9518e6facf9026ee9d9bce2850ae179`.
The settings and calibration page hash matched before and after.

At 1 ms/div, the same Windows Qt session received five complete
1,024-sample records with Auto, rising edge and trigger level 2400.
Its four steady host delivery intervals were 201–211 ms
(4.84 records/s, up from 3.12). The capture reached Complete in
151–160 ms and the transfer took 49–50 ms on those records.
The PC measured 1,002.04 Hz on the onboard signal and generated a
1,024-point spectrum, native capture and CSV from the final record.

The Windows MainWindow also passed a live COM8 check with the final
desktop code. After writing the timebase and trigger level, the first
live request returned Busy. Studio refreshed settings and retried once,
then displayed six records. The four intervals from the second to sixth
record averaged 220 ms (4.55 records/s), meeting the four-record target.
The final displayed record reported a 221 ms host interval. The samples
and frozen metadata matched the acknowledged Auto, rising, level 2400
setup at 1 ms/div.

The direct USB client at 1 ms/div completed 15 records at 5.27 records/s,
with 186–199 ms from request to delivery and 48.7–49.1 ms for the leased
transfer. It also completed four 100 µs/div and three 100 ms/div records.
The first fast record took 886 ms after the settings change and the
next three arrived at 111–113 ms intervals. Slow records took
1,270–1,296 ms from request to delivery. The legacy and extended reads of
the final record matched, and session expiry restored the prior settings.
These are separate acquisitions with intervals between deliveries.

In local 500 kS/s RUN, the final build handled 5,688 STATUS requests in
three seconds. The median round trip was 0.533 ms and the 95th percentile
was 0.904 ms. The largest observed DMA poll gap was 53,939 cycles against
the 69,631-cycle limit. All four acquisition, transport, panel and
controller fault fields were zero.

#### Initial Studio 2 comparison

The 64,488-byte application verified after programming pages 0–62.
The SHA-256 of the reserved page matched before and after programming.
The programmer's GO command produced a startup fault on this board.
Two explicit software resets booted successfully with acquisition and
USB CDC on COM8.

At 100 µs/div in local RUN (500 kS/s), the board answered 5,520 STATUS
requests in three seconds. The median round trip was 0.546 ms and the
95th percentile was 0.921 ms. The largest observed acquisition poll gap
was 53,974 cycles against the active 69,631-cycle limit. Acquisition,
transport, panel and controller fault fields were zero.

The direct USB client requested Auto captures of the onboard test
signal. Each record held 1,024 ADC codes. The acknowledged timebase,
timer divider and threshold agreed with the frozen metadata. The final
record's legacy and extended raw data matched. The leased download
included the complete read sequence and carried 41.2–43.1 kB/s of sample
bytes across the three settings.

| Timebase | Actual sample rate | Records | Request to delivery | Delivered records/s |
| --- | ---: | ---: | ---: | ---: |
| 100 µs/div | 500 kS/s | 3 | 156–161 ms | 6.28 |
| 1 ms/div | 102.27 kS/s | 6 | 307–355 ms | 3.00 |
| 100 ms/div | 1.024 kS/s | 2 | 1,497–1,525 ms | 0.66 |

The Windows Qt session received five 1,024-sample Auto records at
1 ms/div with the acknowledged trigger settings. Four intervals between
deliveries ranged from 313 to 325 ms, averaging 3.12 records/s against
the design target of four. The lease transfer took 49–51 ms per record.
The board spends time acquiring, processing and updating the LCD between
records. These intervals describe successive records and cannot measure
how many ADC sample times fell between them.

The same Qt run measured 1,002.05 Hz on the onboard signal and produced
a valid 1,024-point FFT, a native capture file and a CSV with the frozen
trigger settings and delivery interval. The onboard signal checks data
flow and analysis without establishing calibrated analog accuracy.

## Studio 1.0 reference verification

| Check | Result |
| --- | --- |
| Build | Windows 11 x64 Release with Qt 6.8.3, MSVC toolset 14.44.35207, CMake 4.1.1-msvc1 and Ninja. |
| Capture data | Q16 signed rounding, bounds, measurements, CSV, versioned file round trip and malformed-file rejection passed. |
| Protocol simulation | Golden INFO, fragmented/coalesced input, CRC recovery, same-ID replay, pending-to-ready polling, stale job protection, repeat, cancellation, timeout, link loss and reconnect passed. Simulated unplug exercises the application's transport-loss path. |
| Native interface | Zoom, fit, keyboard navigation and cursors, open/recovery, and resize at effective 100%, 150% and 200% scale passed. |
| Live USB | Native UI selected the port, completed INFO, displayed a full held record, and disconnected. An independent protocol client matched all 1,024 codes, converted values, time points and frozen fields. |
| Live capture | Single and three consecutive distinct 1,024-sample records passed. The held record survived saving and reopening exactly. Exclusive COM access and reopening the same record after a new session passed. |
| Portable package | The ZIP's SHA-256 and all manifest hashes matched. A new extraction started with a Windows-only PATH. All Qt modules, the Windows platform plugin, and the MSVC runtime loaded from the extraction directory. |

The R6 bench runs observed 64–67 ms from the Single request to the first Complete status and 48–52 ms for a complete leased transfer. The two intervals exclude the intervening readiness wait and any requested delay between captures. They describe the onboard signal and reference setup. Repeated captures have gaps. The scope firmware for this comparison was the v2.0.0 baseline.

The Studio 1.0 ZIP was 11,065,082 bytes. SHA-256 `55af4b69ebda670b497fd8816df777f0760b8146cf2c64c0d5358884713923d9`. The cleanup changed the build and packaging files. The application source was identical to the version used for the live USB tests. The rebuilt ZIP passed a manifest check and portable launch on the development PC.

**Clean Windows launch: unverified.** A separate Windows 11 installation without Qt or Visual Studio was unavailable. On 3 November 2026, the project owner accepted the live USB results and the portable launch on the development PC as sufficient to close the 1.0.0 development goal. The independent clean-machine check is deferred.

## Clean Windows launch check

On a Windows 11 x64 PC or VM without Qt or Visual Studio, run Get-FileHash -Algorithm SHA256 on the ZIP and compare the result with the supplied checksum. Extract the ZIP in File Explorer, launch DSO138Studio.exe and confirm that the demo waveform, axes and measurements appear. Record the checksum, Windows build and result.
