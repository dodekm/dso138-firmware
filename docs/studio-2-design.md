# Studio 2 engineering plan

## Baseline

The starting checkout is `0af20e6`. The firmware matched the
`v2.0.0` tag. Studio 1.0.0 uses Qt Widgets and a single asynchronous
serial session. An isolated CubeIDE 2.2.0 Release build reproduced the
retained 64,408-byte R6 image byte for byte with the private board
configuration. Its documented configured build uses 112 bytes of
initialized SRAM, 13,192 bytes of BSS and a 2,048-byte stack reservation.
The application region ends at
`0x0800FC00`, leaving the final 1 KiB for settings and calibration.
The reference test and build evidence is in [Release verification](release-readiness.md).

The previous R6 bench tests measured 40.37–42.76 kB/s of raw sample bytes
and 47.89–50.74 ms for the 1,024-sample read sequence. The target's Single
completion took roughly 79–80 ms in those runs. These intervals leave
about 7.7–7.9 records per second before other host and device delays.
After the board reappeared on USB, the running firmware returned the
legacy INFO response and rejected opcode 08 as Unsupported. In a
three-second RUN sample it answered 5,129 STATUS requests with no
controller fault and a median round trip of 0.597 ms (95th percentile
0.861 ms). Acquisition and presentation states advanced. Repeated
one-shot capture timing still uses the historical comparison because the
reference scope was in local RUN during this check.

The [STM32F103x8/xB data sheet](https://www.st.com/resource/en/datasheet/stm32f103c8.pdf)
specifies USB full speed, 64 KiB Flash for x8, and 20 KiB SRAM. The
[USB 2.0 specification](https://www.usb.org/document-library/usb-20-specification)
and the [board's USB contract](usb-hardware.md) govern transport and
electrical design. The R6 reference uses the opt-in stock-board bench
profile. The standalone release guards, ADC/DMA arrangement, and USB
clock and interrupt setup are retained for the settings milestone.

## Settings and ownership

Keep protocol v1 framing, INFO bytes, and operations 01–07 intact. Studio
1.0.0 verifies the reserved INFO bytes are zero during connection. The
new client probes a separate readback command; Unsupported selects legacy
behavior. Use the existing response cache for retry protection, and
expose write support independently of whether a write can proceed now.

The app displays a current settings snapshot separately from capture
metadata. Reads come from the foreground controller after panel handling.
Remote writes use validated controller operations and an expected settings
revision. Set controls are evaluated at a safe acquisition boundary.
Keep a held waveform immutable until an explicit action starts a new
capture. The controller also owns lease cancellation and pending jobs.
Keep remote changes out of persistent storage by default.
Only an idle held controller accepts timebase and trigger setting
changes. Atomic groups keep partial writes out of the firmware. A panel
press takes control before applying the local gesture. Disconnect or
five seconds without a valid request restores the pre-session settings.
An explicit local RUN pause leaves the instrument held until the
operator resumes from the panel.

| Setting | Live readback | Remote write priority |
| --- | --- | --- |
| Timebase and timer rate | Catalogue selection, nominal division, timer clock and divider | Timebase first. Timer rate is derived from the selected hardware plan. |
| Trigger | Mode, edge, raw threshold and position | Mode is paired with the timebase. Edge, threshold and position are a separate atomic group. |
| Acquisition | RUN intent and controller state | The host can explicitly pause local continuous RUN and request one-shot jobs. The panel owns resuming local RUN. |
| Coupling and sensitivity | Debounced physical state | Read-only hardware switches. |
| Calibration and stored settings | Calibration stays attached to the capture | Existing local workflow. No implicit saves. |

## Live view experiment

Start with successive Single records through the existing lease and
one-command-at-a-time session. Measure record completion, transfer,
presentation, and delivered updates separately. Keep a bounded history
and show gaps between records. At 500 kS/s, 16-bit samples require
1,000,000 sample bytes per second. The measured R6 transfer path supplies
about 4% of that. Compare packed or batched block transfer and a separate
sample stream only after measuring the actual live-record bottleneck.
Any streaming experiment must preserve DMA poll and buffer ownership
invariants and disclose the rates at which continuity is verified.
When available, the selected-mode one-shot opcode lets Auto, Normal and
Single retain their trigger semantics in a host-managed sequence.
Extended exports freeze the selected timebase index and ADC threshold
at acquisition and leave the legacy metadata response unchanged.

| Transport option | Resource and timing implications |
| --- | --- |
| Successive leased records | Reuses the existing stopped-producer frame, packet bounds, and retry policy. The R6 measurements establish the transfer comparison. Records have acquisition and presentation gaps. |
| Packed block transfers | Three bytes encode two 12-bit samples. With the existing per-reply metadata and one 64-byte frame, a packed reply can carry 28 codes rather than 22. That can reduce transaction count, but it leaves the one-request round trip and the time between records. |
| Dedicated stream | At 500 kS/s, packed samples alone require 750 kB/s. A DMA half fills in 1.024 ms, so packet-memory writes and USB service would have to fit alongside the existing copy, trigger and foreground polling work. This path needs a new ownership and overrun contract. |

The current build uses successive records. Its measured Flash margin is
16 bytes with all agreed controls and standalone features enabled. A
new sample stream would require additional protocol, buffering and
timing validation. The scope view reports the acquisition sample rate
separately from the PC's delivered-record rate. A host interval between
records includes acquisition, presentation, transfer and scheduling.

## Configured build evidence

Release and Debug produced identical 64,496-byte binaries. The
final storage page and 2,048-byte
stack reservation stay at their R6 boundaries. The new build's
`.data` is 112 bytes and `.bss` is 13,136 bytes. Compared with the
R6 compiler reports, the `main`, foreground idle, and arm frames are
unchanged. The USB service's own frame decreased from 112 to 64 bytes.
The LCD one-shot label helper adds a nested 32-byte frame while
reducing the containing formatting frame from 192 to 176 bytes. These
per-function figures are useful for checking the stack budget. The
R6 runtime watermark and poll-gap results are historical bench data.

The first Studio 2 board run delivered 3.12 records/s at 1 ms/div.
The held-capture path presented the finished waveform, then scheduled
another redraw after the controller entered HOLD. The second redraw
delayed the export lease. The controller now keeps the completed scene
for a held capture and still redraws for continuous RUN and its waiting
states. With the same Qt session and trigger settings, the reference
board delivered 4.84 records/s. See the
[reference-board results](../host/windows/VERIFICATION.md#reference-board-results).

## Acceptance checks

- Link a fully enabled build within the 64,512-byte application region.
  Compare initialized and uninitialized SRAM and audit the existing stack
  reservation. Keep standalone acquisition, controls, storage and display
  regression evidence separate from build-size evidence.
- Exercise an original Studio waveform exchange and the new client's
  legacy fallback. Cover malformed values, conflicts, identical retries,
  panel changes, busy acquisition, loss of the serial session, and readback
  agreement with the settings frozen in the next capture.
- Aim for current settings to appear within one second at idle. At the
  representative 1 ms/div periodic-signal setup, target at least four
  displayed records per second and report the observed rate, latency and
  gaps. Repeat across a fast and a slow timebase. Establish timing
  regression against the R6 poll-gap budget rather than against GUI FPS.
- Validate measurement and FFT results against generated signals and
  preserve capture data across native-file and CSV export. Run Windows
  package and high-DPI checks separately from board checks.

Firmware flashing and publication require separate authorization. This
design record tracks proposed behavior and acceptance checks. Verified
results belong in the associated release and protocol documents.
