# DSO waveform protocol v1

This document describes the host-facing command protocol implemented by
`dso_usb_requests.cpp` and serviced by `dso_usb_service.cpp`;
`dso_usb_protocol.cpp` supplies the CRC adapter. The transport supplies
configured-session identity and nonblocking packet read/write. See the
[USB hardware and driver contract](usb-hardware.md) for the opt-in
stock-board trial. Studio 2 adds optional live settings operations to
the original waveform and Single-capture commands.

The shared version, magic bytes, frame layout/bounds, opcode/status values,
GET_SETTINGS capability/readiness bits and samples-per-read limit are defined
in `shared/include/dso_usb_protocol_contract.hpp`; it aliases record capacity
from `shared/include/dso_capture_contract.hpp`. The ADC resolution reported by
INFO comes from `shared/include/dso_adc_contract.hpp`. Host capture-state values
and other stable instrument codes and capture flags are in
`shared/include/dso_instrument_codes.hpp`. INFO, STATUS, GET_SETTINGS,
SET_SETTINGS, SET_TRIGGER, HOLD_LOCAL_RUN, capture-job acknowledgements,
capture-metadata and READ_SAMPLES fixed layouts are also shared; each endpoint
retains its own field validation and command handling.

## Framing

Every request and response is one frame of 12–64 bytes:

| Offset | Size | Meaning |
| --- | ---: | --- |
| 0 | 2 | Magic bytes `44 53` (ASCII `DS`) |
| 2 | 1 | Protocol version, `1` |
| 3 | 1 | Opcode; responses set bit 7 |
| 4 | 2 | Request ID, little-endian, nonzero |
| 6 | 2 | Payload size, little-endian, at most 52 bytes |
| 8 | 4 | CRC-32/IEEE, little-endian |
| 12 | 0–52 | Payload |

CRC-32/IEEE uses reflected polynomial `EDB88320`, initial value `FFFFFFFF`, final XOR `FFFFFFFF`, and covers header bytes 0–7 followed by payload bytes. It excludes the four-byte CRC field. A response echoes the request ID and uses `request opcode | 80`. Its payload starts with one status byte: `0 Ok`, `1 Invalid`, `2 Busy`, `3 Stale`, `4 Unsupported`, `5 Timeout`, `6 Cancelled`, `7 Fault`.

The firmware and Studio share the version-1 opcode and status code definitions in `shared/include/dso_usb_protocol_contract.hpp`; the command table below documents those wire values.

Golden request vector: `INFO`, request ID 1, empty payload is `44 53 01 01 01 00 00 00 24 7D 48 94`. The CRC is `94487D24` and is serialized least-significant byte first.

All multi-byte payload integers are little-endian. Commands have one outstanding request at a time. The host must not send a new request until it receives the response to the previous one. If the response times out, retransmit the identical frame with the same ID; the device caches and replays the last response. Reusing that ID with different opcode or request bytes returns `Invalid` and does not repeat the operation. Request IDs must be nonzero and should advance for each new command.

The parser accepts fragmented frames and retains coalesced bytes after processing one command. It reads no more than 16 transport bytes and processes no more than one command per foreground `step()`. Invalid magic is discarded with bounded resynchronization. Invalid version/length or CRC produces `Invalid` when an ID can be read; bad-CRC resynchronization advances one byte to find a later magic sequence. If the TX transport is busy, no more RX bytes are consumed and queued tail bytes remain intact.

## Commands

Payload sizes below exclude the outer frame. Command response payloads always begin with the status byte.

| Opcode | Request payload | Response payload after status | Description |
| ---: | --- | --- | --- |
| `01 INFO` | Empty | `protocolVersion:u8`, `maxSamples:u16`, `rawBits:u8`, `maxSamplesPerRead:u8`, `reserved:u16` | Returns protocol version 1, 1024 samples, 12 raw bits, maximum chunk 22 and zero reserved bytes. The shared contract defines the body and status-prefixed reply offsets. |
| `02 STATUS` | `job:u32` (`0` means current) | `uiState:u8`, `controllerFault:u8`, `work:u8`, `hostState:u8`, `job:u32`, `frameAvailable:u8`, `frameCount:u16`, `storageBusy:u8` | The shared contract defines request and reply offsets. Host state values follow `HostCaptureState`: None 0, Pending 1, Complete 2, Cancelled 3, Fault 4. For the matching job, a timed-out capture returns status `Timeout`. |
| `03 BEGIN_FRAME` | Empty | Exactly 47 bytes of metadata | Leases an immutable, held frame. Returns `Busy` unless the controller permits export. Metadata fields are listed below. |
| `04 READ_SAMPLES` | `token:u32`, `offset:u16`, `count:u8` | `token:u32`, `offset:u16`, `count:u8`, `samples[count]:u16[]` | Reads 1–22 raw codes from the active frame lease. Codes are 0–4095. The request range must fit the frame. |
| `05 END_FRAME` | `token:u32` | Empty | Releases only the matching export lease. |
| `06 CAPTURE_SINGLE` | `timeoutMs:u32` | `job:u32` | Requests one host-owned Single capture. Timeout must be 1–600000 ms and is absolute; STATUS polling does not extend it. |
| `07 CANCEL_CAPTURE` | `job:u32` | Empty | Cancels only the matching host-owned job while it is pending. It cannot cancel a local acquisition. |
| `08 GET_SETTINGS` | Empty | Settings snapshot | Discovers optional settings support and reports the current controller and physical settings. Legacy firmware returns `Unsupported`. |
| `09 SET_SETTINGS` | `revision:u16`, `timebaseIndex:u8`, `triggerMode:u8` | Settings snapshot on success | Applies the timebase and mode together. A rejected request returns only the status. |
| `0A CAPTURE_SELECTED` | `timeoutMs:u32` | `job:u32` | Starts one host-owned job using the selected trigger mode. Status, timeout and cancellation follow CAPTURE_SINGLE. |
| `0B BEGIN_FRAME_EXTENDED` | Empty | Exactly 51 bytes of metadata | Same lease as BEGIN_FRAME, with additional frozen timebase and trigger-level fields. |
| `0C SET_TRIGGER` | `revision:u16`, `edge:u8`, `level:u16`, `position:u16` | Settings snapshot on success | Applies edge, level and position together. Values and guards match the current settings fields below. |
| `0D HOLD_LOCAL_RUN` | `revision:u16` | Settings snapshot on success | Explicitly stops local continuous Auto/Normal RUN. The panel owns restarting RUN. |

The complete `BEGIN_FRAME` response payload is 48 bytes including status.
Shared body offsets and extended response lengths are defined in
`shared/include/dso_usb_protocol_contract.hpp` and used by both encoder and decoder.

| Payload offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | Status |
| 1 | 4 | Export token |
| 5 | 2 | Sample count |
| 7 | 2 | Pretrigger sample index |
| 9 | 4 | Timer clock in Hz |
| 13 | 4 | Timer divider |
| 17 | 4 | Acquisition epoch |
| 21 | 4 | Trigger sample sequence |
| 25 | 4 | Physical settings generation |
| 29 | 4 | Nominal microvolts per division |
| 33 | 4 | Signed zero code in Q16 |
| 37 | 4 | Microvolts-per-code gain mantissa in Q16 |
| 41 | 1 | Gain shift |
| 42 | 1 | Calibration kind: Unavailable 0, Nominal 1, Measured 2 |
| 43 | 1 | Coupling: Unknown 0, DC 1, AC 2, Ground 3 |
| 44 | 1 | Trigger mode: Normal 0, Auto 1, Single 2 |
| 45 | 1 | Trigger edge: Rising 0, Falling 1 |
| 46 | 1 | Capture flags from `shared/include/dso_instrument_codes.hpp`: bits 0–2 and 4–7 reserved and zero; bit 3 edge triggered |
| 47 | 1 | Reserved, zero |

BEGIN_FRAME_EXTENDED appends the following fields to the same response
payload. The four extra bytes fit within the original frame size limit.

| Payload offset | Size | Field |
| ---: | ---: | --- |
| 48 | 1 | Timebase catalogue index at arm |
| 49 | 2 | Trigger level in ADC codes at arm |
| 51 | 1 | Reserved, zero |

`CAPTURE_SINGLE` asks the controller for a one-shot in idle HOLD. It uses
the selected timebase, trigger edge, level and position with Single
trigger semantics. The host leaves the local RUN/HOLD intent in the
controller's hands. Connection-session changes, a five-second idle
lease timeout, timeout expiry, or local invalidation revoke the host's
own lease/job. Disconnect and idle expiry cancel only a pending host job
created through this protocol.

`CAPTURE_SELECTED` uses Auto's trigger wait and fallback when Auto is
selected. Normal and Single wait for an edge. Each host job still
completes at most once and leaves the local scope in HOLD.

## Current settings extension

GET_SETTINGS and successful SET_SETTINGS replies share one 32-byte body.
Offsets here exclude the status byte.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | Settings schema, currently 1 |
| 1 | 1 | Writable capabilities. Bit 0 timebase, 1 mode, 2 edge, 3 level, 4 position, 5 local RUN pause. |
| 2 | 1 | UI state. Hold 0, waiting for controls 1, waiting for backend 2, arm pending 3, acquiring 4, frame ready 5, fault 6. |
| 3 | 1 | Flags. Bit 0 RUN intent, 1 qualified physical settings, 2 setting write admissible now, 3 CAPTURE_SELECTED and BEGIN_FRAME_EXTENDED supported, 4 local RUN pause admissible now. |
| 4 | 1 | Selected timebase index |
| 5 | 1 | Number of timebase entries |
| 6 | 1 | Selected mode. Normal 0, Auto 1, Single 2. |
| 7 | 1 | Rising edge 0, falling edge 1 |
| 8 | 2 | Trigger level in ADC codes (0–4095) |
| 10 | 2 | Trigger position in permille (0–1000) |
| 12 | 4 | Nominal sensitivity in µV/div, zero when the switches are unqualified |
| 16 | 4 | Selected timer clock in Hz |
| 20 | 4 | Selected timer divider |
| 24 | 1 | Coupling. Unknown 0, DC 1, AC 2, ground 3. |
| 25 | 1 | Catalogue ID. `ClassicCatalogueId` (1) identifies the classic 10/20/50 µs/div sequence across successive decades. |
| 26 | 2 | Settings revision, compared by equality within the session |
| 28 | 4 | Nominal timebase in µs/div |

The canonical writable-capability and readiness flag values are in
`shared/include/dso_usb_protocol_contract.hpp`; Studio keeps the
`dso::protocol::settings_writable` and `dso::protocol::settings_flags` names as
aliases. Capture metadata flags remain a distinct field with different meanings.
The shared `settings_body` constants name the 32-byte body offsets and the
33-byte reply size, including the leading status byte.
The permitted trigger-position range is defined in
`shared/include/dso_settings_contract.hpp`. The ordered classic timebase values
and their catalogue count are defined in
`shared/include/dso_timebase_contract.hpp`; firmware and Studio consume both
shared contracts.

The timer clock and divider can both be zero while the selected plan is
unavailable. The settings apply to the next acquisition. Held
waveform data and its capture metadata stay frozen. A write requires the
last acknowledged revision and idle HOLD with no lease, storage,
calibration, or local button gesture. A stale revision returns
`Stale`; read current settings again before offering another write.
Retry a lost acknowledgement with the same ID and payload.

The first write saves the local selections in RAM. A local button press
restores those selections before applying the panel action. Transport
session loss or five seconds without a valid request also restores the
selections. Coupling and sensitivity follow the physical switches
throughout.

HOLD_LOCAL_RUN is an explicit acquisition action. It uses the same stop
path as the panel and preserves a completed record if available. HOLD
remains in effect after disconnect. A pending host-owned job or a local
Single acquisition cannot be paused through this command.

## Interpreting captures

Use [DSO138 Studio for Windows](../host/windows/README.md) to download held captures, request one-shots or acquire successive records over USB CDC.

Raw sample words are ADC codes. For zero-based sample index `i`, time
relative to the trigger is `(i - pretrigger) * divider / timerHz`.
Studio applies the transmitted Q16 zero, gain and shift when calibration
is available. Calibration metadata describes the configured conversion.
Successive captures have acquisition and presentation intervals between
records.

## Hardware smoke results

Current Studio 2 checks cover settings readback, acknowledged writes,
successive captures, raw data agreement between legacy and extended
exports, session rollback and acquisition under USB traffic. See the
[current bench results](../host/windows/VERIFICATION.md#reference-board-results).

R6 live protocol smoke passed fragmented frames, exact duplicate replay, conflicting-ID rejection, bad-CRC rejection and resynchronization. Three verified restart/CDC handshakes reached INFO/STATUS readiness in 4.96–5.52 s including probe work. The R6 DTR test deasserted DTR with a frame lease active, closed/reopened CDC, observed `Stale` for the old lease token, recovered INFO/STATUS with no job, and exported the same held record with matching full metadata and RAW hash. This is a control-line/session-loss check; it does not test USB bus suspend/resume.

The R6 Python host test completed three 1,024-sample Single captures (epochs 370–372). Acknowledgments took 1.07–1.17 ms, request-to-completion took 79.14–80.20 ms, and raw reads took 47.89–50.74 ms (40.37–42.76 kB/s of sample bytes). Cancellation returned `Cancelled`. A 1 ms deadline returned `Timeout` and left no pending job. See the [Studio 2 verification](../host/windows/VERIFICATION.md) for the current firmware's remote settings and capture results.

The R6 FFT oracle matched peak bin 1 for the exported held record (observed-to-predicted level difference -0.0022 dB). Firmware returned the same raw samples and frozen metadata across Scope/Spectrum/Scope domain checks. A debug-attached timing observation was 64.82 million DWT cycles / 1.75 s including probe work for Spectrum presentation, and 64.96 million cycles / 1.78 s on return. These are bounded end-to-end observations, not FFT-only duration. Screen counters advanced, but no independent visual verification of the scope display or analog metrology was performed; calibration metadata was nominal, not measured. Electrical production compliance and fast repeated acquisition remain unqualified.
