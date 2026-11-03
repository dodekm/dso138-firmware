# DSO waveform protocol v1

This document describes the compact host-facing command protocol implemented by `dso_usb_protocol.cpp`. A transport adapter supplies configured-session identity and nonblocking packet read/write; the protocol itself neither selects a USB class nor configures STM32 registers. Hardware wiring and the limits of the opt-in stock-board trial are described in [USB hardware and driver contract](usb-hardware.md). The protocol supports read-only waveform access and a host-requested single capture. It has no commands for changing settings or writing Flash.

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

Golden request vector: `INFO`, request ID 1, empty payload is `44 53 01 01 01 00 00 00 24 7D 48 94`. The CRC is `94487D24` and is serialized least-significant byte first.

All multi-byte payload integers are little-endian. Commands have one outstanding request at a time. The host must not send a new request until it receives the response to the previous one. If the response times out, retransmit the identical frame with the same ID; the device caches and replays the last response. Reusing that ID with different opcode or request bytes returns `Invalid` and does not repeat the operation. Request IDs must be nonzero and should advance for each new command.

The parser accepts fragmented frames and retains coalesced bytes after processing one command. It reads no more than 16 transport bytes and processes no more than one command per foreground `step()`. Invalid magic is discarded with bounded resynchronization. Invalid version/length or CRC produces `Invalid` when an ID can be read; bad-CRC resynchronization advances one byte to find a later magic sequence. If the TX transport is busy, no more RX bytes are consumed and queued tail bytes remain intact.

## Commands

Payload sizes below exclude the outer frame. Command response payloads always begin with the status byte.

| Opcode | Request payload | Response payload after status | Description |
| ---: | --- | --- | --- |
| `01 INFO` | Empty | `protocolVersion:u8`, `maxSamples:u16`, `rawBits:u8`, `maxSamplesPerRead:u8`, `reserved:u16` | Returns protocol version 1, 1024 samples, 12 raw bits, and maximum chunk 22. No fabricated device identity is returned. |
| `02 STATUS` | `job:u32` (`0` means current) | `uiState:u8`, `controllerFault:u8`, `work:u8`, `hostState:u8`, `job:u32`, `frameAvailable:u8`, `frameCount:u16`, `storageBusy:u8` | Host state values follow `HostCaptureState`: None 0, Pending 1, Complete 2, Cancelled 3, Fault 4. For the matching job, a timed-out capture returns status `Timeout`. |
| `03 BEGIN_FRAME` | Empty | Exactly 47 bytes of metadata | Leases an immutable, held frame. Returns `Busy` unless the controller permits export. Metadata fields are listed below. |
| `04 READ_SAMPLES` | `token:u32`, `offset:u16`, `count:u8` | `token:u32`, `offset:u16`, `count:u8`, `samples[count]:u16[]` | Reads 1–22 raw codes from the active frame lease. Codes are 0–4095. The request range must fit the frame. |
| `05 END_FRAME` | `token:u32` | Empty | Releases only the matching export lease. |
| `06 CAPTURE_SINGLE` | `timeoutMs:u32` | `job:u32` | Requests one host-owned Single capture. Timeout must be 1–600000 ms and is absolute; STATUS polling does not extend it. |
| `07 CANCEL_CAPTURE` | `job:u32` | Empty | Cancels only the matching host-owned job while it is pending. It cannot cancel a local acquisition. |

The complete `BEGIN_FRAME` response payload is 48 bytes including status:

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
| 46 | 1 | Flags: bits 0–2 reserved and zero; bit 3 edge triggered |
| 47 | 1 | Reserved, zero |

There is no host command for arbitrary trigger or front-panel changes. `CAPTURE_SINGLE` asks the existing controller to start a one-shot only when local control state and acquisition ownership allow it. The scope must be in HOLD and foreground work must be idle; a request while RUN is active can return `Busy`. The host does not take over local acquisition or switch the scope to HOLD. Local button actions retain priority. Connection-session changes, a five-second idle lease timeout, timeout expiry, or local invalidation revoke the host's own lease/job. Disconnect and idle expiry cancel only a pending host job created through this protocol.

## MATLAB client

`host/matlab/DsoUsbClient.m` is a small MATLAB `serialport` client. The USB backend must expose a COM/serial interface; the class does not supply a kernel driver, MATLAB MEX binary, or USB class implementation. The host can query INFO, download a held frame, request/poll a one-shot, and explicitly run repeated one-shot captures with a caller-specified gap. It leaves DTR unchanged.

The `Raw` field is always the returned ADC code vector. `TimeSeconds` is the nominal rational timer axis

```text
(0:count-1) - pretrigger, multiplied by divider / timerHz
```

when expressed as a column vector. `VoltageMicrovolts` and `VoltageVolts` are empty when calibration kind is Unavailable. Otherwise the client applies the transmitted Q16 zero, gain, and shift using integer arithmetic with round-to-nearest, ties away from zero. Calibration metadata describes the firmware's configured profile; it does not independently establish analog accuracy.

Repeated `capture` calls have acquisition/presentation dead time. The optional `gapSeconds` in `captureRepeated` adds an explicit delay between those calls; it is not a continuous recorder and makes no throughput guarantee.

## Hardware smoke results

R6 live protocol smoke passed fragmented frames, exact duplicate replay, conflicting-ID rejection, bad-CRC rejection and resynchronization. Three verified restart/CDC handshakes reached INFO/STATUS readiness in 4.96–5.52 s including probe work. The R6 DTR test deasserted DTR with a frame lease active, closed/reopened CDC, observed `Stale` for the old lease token, recovered INFO/STATUS with no job, and exported the same held record with matching full metadata and RAW hash. This is a control-line/session-loss check; it does not test USB bus suspend/resume.

The R6 Python host test completed three 1,024-sample Single captures (epochs 370–372): acknowledgments took 1.07–1.17 ms, request-to-completion 79.14–80.20 ms, and RAW reads 47.89–50.74 ms (40.37–42.76 kB/s of sample bytes). Cancellation returned `Cancelled`; a 1 ms deadline returned `Timeout`, and no job remained pending. MATLAB R2025b with the current public client exported held-frame data and completed a host Single capture. Its `captureRepeated(3,uint32(3000),0)` run returned three 1,024-sample records in 3.361 s with distinct increasing epochs 377–379 and monotonic time axes. Each capture is a separate Single operation with acquisition/presentation dead time, not continuous or gap-free recording. Historical R4/R5 results also passed; the R5 repeat check took 2.524 s.

The R6 FFT oracle matched peak bin 1 for the exported held record (observed-to-predicted level difference -0.0022 dB). Firmware returned the same raw samples and frozen metadata across Scope/Spectrum/Scope domain checks. A debug-attached timing observation was 64.82 million DWT cycles / 1.75 s including probe work for Spectrum presentation, and 64.96 million cycles / 1.78 s on return. These are bounded end-to-end observations, not FFT-only duration. Screen counters advanced, but no independent visual verification of the scope display or analog metrology was performed; calibration metadata was nominal, not measured. Electrical production compliance and fast repeated acquisition remain unqualified.
