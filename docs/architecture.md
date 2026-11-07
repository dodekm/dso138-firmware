# Architecture overview

## Runtime shape

The firmware uses C++17, static allocation and a foreground event loop. It has no
RTOS and the application does not use a heap. Platform modules own startup, MCU
peripheral setup and interrupt routing. The application core initializes the
display, panel, acquisition and user interface for the selected board profile.

`dso_platform_startup.cpp` owns the C++ entry path, peripheral initialization
order and foreground loop. `dso_platform_clock.cpp` owns the fixed HAL clock
tree, which the application validates before board activation.
`dso_platform_msp.cpp` owns HAL peripheral pins and DMA setup;
`dso_platform_interrupts.cpp` routes interrupt handlers. `dso_app.cpp`
coordinates foreground lifecycle and services the `dso_ui_controller.cpp`
interface.
The C startup boundary in `dso_app.h` names stable `DsoBenchStatus.fault`
diagnostic values; `dso_app_fault()` still publishes the first terminal reason.

### Firmware ownership

#### Acquisition ownership

`DmaAcquisition` owns DMA storage, capture assembly and the frozen sample record.
`AcquisitionPort` supplies hardware operations. The private
`recall_staging_bytes()` view is valid only between `begin_recall_staging()` and
`end_recall_staging()`, so storage code does not depend on the workspace union
representation. The calibration callback checks that Recall is the active
owner before reading the view. `dso_acquisition.hpp` forward-declares
`PanelUpdate`; implementation code includes the front-panel contract where it
inspects event fields.

#### Shared instrument and record values

`dso_physical_settings.hpp` defines coupling and the nominal front-panel
settings snapshot independently of the scan/button API. Its
`same_physical_settings()` helper compares the complete generation, scale and
coupling identity; acquisition, calibration, and UI owners retain their own
readiness and invalidation policy. `dso_adc_codes.hpp`
keeps the firmware-facing `AdcMax` name as an alias to
`shared/include/dso_adc_contract.hpp`. That shared contract owns the ADC
resolution and maximum code used by sampling, panel decoding, scaling,
calibration, trigger validation, USB capability reporting and Studio. Studio's
`instrument_limits.hpp` preserves its existing names as aliases to the same
contract. `dso_trigger.hpp` defines trigger mode and edge values shared by
capture and UI settings, alongside the sample-domain crossing helper. The UI
model uses this contract without importing the capture engine.
`dso_u32_counter.hpp` owns the 32-bit counter half-range used to reject
ambiguous elapsed-time intervals across panel polling, DMA deadlines, DWT waits
and LCD transfers; each module retains its timing policy.

`dso_capture_contract.hpp` owns the maximum record length shared by acquisition,
display and host export. `dso_capture_config.hpp` defines the `CaptureConfig`
value independently of the capture engine and aliases that capacity. The USB
endpoint and Studio codec use `dso_usb_protocol_contract.hpp` for their shared
protocol version, frame/chunk bounds, opcode values and status values; its
capture-capacity name aliases the record contract. It also owns the GET_SETTINGS
writable-capability and readiness flag bits. Firmware's `UsbOpcode` and
`UsbStatus` and Studio's `protocol::Opcode`, `protocol::Status`,
`protocol::settings_writable` and `protocol::settings_flags` remain local names
or aliases, so application code keeps its existing names while both endpoints
use one wire-contract definition. The GET_SETTINGS body offsets and response
length are shared by the firmware encoder and Studio decoder; each side keeps
its own field validation. The same contract owns the base and extended capture
metadata offsets, body sizes and status-prefixed response lengths, plus the
READ_SAMPLES request fields and chunk-response prefix.
`dso_instrument_codes.hpp` owns the stable mode, edge, coupling, calibration,
UI-state, host capture-state and capture-flag byte codes shared by USB and
Studio record files.
Studio keeps its `instrument_code` aliases; firmware retains its domain enums
and checks their serialized values at compile time.
`dso_frame_metadata.hpp` combines capture configuration with
frozen physical and timebase settings, allowing analysis and projection to
avoid importing the full capture API. `dso_voltage_calibration.hpp` defines
calibration kind, profile layout and voltage conversion contracts independently
of waveform projection. `dso_nominal_front_end.hpp` combines those values with
`dso_physical_settings.hpp` without importing display scaling.
`dso_spectrum.hpp` uses the projection-limit contract and forward-declares the
frame/projection values; `dso_spectrum.cpp` includes scaling where it accesses
the completed projection fields.
`shared/include/dso_voltage_math.hpp` owns the signed Q16 gain-rounding
primitive shared by firmware calibration, projection and measurements and by
Studio's sample-voltage conversion. `dso_calibration_contract.hpp` owns the
shared zero-code, gain-shift and mantissa bounds; each layer retains its own
calibration-kind and lifecycle policy.

Measurement and calibration interfaces depend on these value headers;
`dso_calibration.cpp` owns profile validation/conversion and calibration
fit/bank operations. Its `calibration_physical_valid()` query checks profile
eligibility independently of sample-summary validation; the session uses that
query instead of manufacturing a calibration point to validate switch values.
`dso_calibration_session.hpp`, `dso_ui_model.hpp` and
`dso_ui_controller.hpp` include the physical-settings value contract directly
and forward-declare `PanelUpdate`. The UI model and controller also
forward-declare `ButtonEvents`; implementation units include
`dso_front_panel.hpp` where they inspect event fields.
`dso_physical_settings.hpp` owns the ordered nine-entry 10 mV/div to 5 V/div
catalogue and scale step. Nominal eligibility keeps its existing arithmetic
predicate, which is compile-time checked against the catalogue used for
persisted calibration DC-slot order; another assertion pins the slot count.
`dso_calibration.hpp` owns the calibration-reference and validation-tolerance
input bounds shared by the fit, session and UI-model implementations.

#### Front-panel input ownership

`dso_stm32_panel.cpp` owns the ADC2 and GPIOB adapter. `PanelSampler` sequences
bounded, nonblocking conversions through that adapter for SEN2, SEN1 and
coupling, validates the scan epoch and age, then pairs the readings with one
GPIOB input snapshot.
`FrontPanel` coordinates the switch filters and debounced buttons from
`dso_controls.cpp`, waits for analog settling, and publishes a new physical
settings generation only after all switch positions qualify. Board-specific
sense bands and timing are supplied at the application composition point in
`dso_app.cpp`.

`ScopeController::panel_update()` invalidates an active export or host capture
before forwarding a changed or invalid physical tuple to calibration,
acquisition and UI state. `PanelSuspension` owns the sampling pause boundary:
storage pauses require fresh switch qualification after resume, while short
preparation pauses discard partial scans and retain the normal poll-gap checks.
The sampler owns no UI actions; the controller consumes the resulting button
events separately from the hardware scan.

#### Binary encoding and checksums

`shared/include/dso_binary_endian.hpp` provides portable fixed-width
little-endian primitives for firmware persistence and USB commands, and Studio's
Qt-facing wire adapter. `shared/include/dso_crc32.hpp` owns the common CRC-32/IEEE
initial state, byte update and final XOR used by firmware USB, calibration,
storage and the Studio protocol codec. Shared contracts own cross-endpoint wire
codes and field layouts, while handlers retain command validation and
application state policy. `dso_usb_requests.cpp` selects a `memcpy` serializer
only for GNU ARM builds advertising unaligned access and little-endian byte
order; other targets use the portable endian helpers. Keep this guard because
the fast path reads and writes native integer and sample representations.
`dso_usb_protocol_contract.hpp` owns the protocol
version, outer-frame magic/offsets, opcodes, statuses, INFO/STATUS, settings
write requests, GET_SETTINGS response, frame-metadata and READ_SAMPLES layouts,
capability bits and frame/chunk bounds; its sample-capacity value aliases
`dso_capture_contract.hpp`. `dso_instrument_codes.hpp` owns the stable byte
codes shared by USB settings, host capture status, capture metadata and Studio
record files. Firmware keeps its local protocol aliases and checks its
domain-enum mappings at compile time; Studio keeps its source-level aliases to
the shared contracts. `dso_settings_contract.hpp` owns the cross-layer
trigger-position bound. `dso_timebase_contract.hpp` owns the classic catalogue
ID, ordered timebase values and derived entry count. Firmware settings replies
read that shared count directly, while scaling converts a validated index to
its time value; Studio's settings and record views use the same catalogue. At compile time,
`dso_scaling.cpp` checks that list against the original 10/20/50 progression
and checks its count against the firmware's offline-generated timer factors.
`dso_timebase_presets.hpp` pins those factors with a fingerprint.
`dso_timebase.hpp` owns `valid_record_timing()`, the shared 1–72 MHz record
validation and PSC/ARR-divider consistency check used by scaling, measurements
and spectrum analysis; this policy does not constrain the generic planner.
`dso_saved_record.cpp` names and checks the
version-5 saved-settings header offsets at compile time and maps its firmware
trigger enums through the shared stable byte codes. The USB request module keeps
its Cortex-M3 fast path and uses the shared byte routines for its portable
fallback. Callers validate byte ranges before encoding or decoding.

#### Foreground application policy

`ScopeController` owns foreground policy for controls, acquisition state,
display projection, storage workflows and USB requests. Its header depends on
the frozen frame value type and forward-declares the DMA owner; implementation
units include the full acquisition API only where they access it.
`RetainedFrameState` groups the UI-owned sample copy, bounded-copy progress and
frozen metadata, calibration, request and timebase. It is marked valid only once
the copy completes. `ProcessingState` groups the active acquisition request and
timebase with the foreground work stage, pending frame-consumption flag and
Spectrum workspace lease. The active DMA record remains owned by
`DmaAcquisition`.
`AnalysisWorkspace` groups active frame calibration, projection and the
incremental measurement/Spectrum engines. It owns signal-analysis state while
`ProcessingState` owns scheduling state.

The controller's `scene()` accessor is a read-only borrow of screen-owned
composition storage; later controller updates may replace its contents.
`dso_ui_controller.cpp`
coordinates controls, acquisition, projection and presentation.
`dso_ui_host.cpp` implements USB capture leases and remote settings through
`HostControlState`, which groups lease/job identity, settings revision and
local-control arbitration. `reconcile_acquisition_readiness()` refreshes the
selected timing plan with acquisition input paused after a timebase change, then
applies capture-inhibit gates for boot storage, disabled acquisition and
unsupported plans.

#### Presentation and persistence

`dso_ui_text.cpp` composes labels and measurement text from controller state.
Its file-local `ScreenTextBuilder` finalizes the terminator, stale suffix and
dirty-range update at scope exit; each builder is kept within one label update.
`dso_ui_storage.cpp` owns storage admission, incremental persistence and
calibration-byte staging through `service_storage()`. `StorageState` groups its
phase, command, status, boot flags and calibration-restore progress.
`UiControllerConfig::storage` is non-owning; the application owns `ScopeStorage`
and keeps it alive longer than the controller. The controller header
forward-declares that type, while implementation units include its concrete
interface where needed.

While storage borrows the DMA workspace, the controller services storage and
returns before normal acquisition polling. The source-unit split preserves one
foreground policy owner while separating presentation and storage
implementation responsibilities.

#### Display

`dso_display_limits.hpp` owns LCD bounds, while
`dso_projection_limits.hpp` owns the configurable waveform-column capacity.
This keeps the screen interface independent of the scaling algorithms.
`dso_display.hpp` preserves the 2.0.2 `DisplayMaxSamples` API as an alias to the
shared capture contract.
`dso_capture_config.hpp` is the single authority for maximum record length used
by acquisition, analysis and waveform scaling. `dso_render_types.hpp` owns the
renderer’s bounded work budget and result values. `dso_screen_scene.hpp` owns
passive snapshot, text-field and dirty-range values independently of the
renderer.

`ScopeScreen` includes the scene contract and forward-declares the concrete LCD
driver. `ScopeController` stores only a screen reference and exposes a borrowed
scene value, without importing the renderer's complete storage layout. Their
implementation files include `dso_scope_screen.hpp` where screen operations
are used; screen implementation includes `dso_display.hpp` where driver
operations are used. `ScopeScreen` owns controller-attached composition reset
and the public render-job lifecycle.

The scene's waveform pointer is a read-only borrow. Later foreground composition
may replace its contents, so the projection stays immutable while a render job
is active. `ScreenTextField` names labels used by the text dirty mask, range
cache and renderer bands; compile-time checks preserve the established bit
allocation and one-to-one band mapping. `dso_scope_render.cpp` owns snapshot
normalization, dirty-tile planning and scanline rasterization. The STM32 display
adapter owns the display bus and bounded DMA transfers.

#### USB

`dso_usb_protocol.hpp` exposes wire version, frame bounds, opcodes, status values
and named outer-frame offsets from the shared protocol contract. That contract
also owns INFO, STATUS, GET_SETTINGS, SET_SETTINGS, SET_TRIGGER, HOLD_LOCAL_RUN,
capture-job acknowledgement, frame-metadata and READ_SAMPLES payload layouts,
with compile-time checks for their field boundaries and response sizes.
Handlers retain command validation and state transitions; positional payload
access uses shared offsets, while sequential host builders append fields in the
documented order.

`dso_usb_protocol.cpp` implements the protocol CRC adapter.
`dso_usb_transport.hpp` separately defines the nonblocking byte-stream port.
The protocol header also includes the transport header to preserve the original
source interface for clients that obtained `UsbStreamPort` from it.
`UsbCdcDevice` owns CDC descriptors, control-transfer state and bounded bulk
queues through `UsbCdcPort`. `Stm32UsbDevice` implements that port and owns the
STM32F103 register/PMA access and endpoint-toggle semantics. The USB vector
calls `irq_notify()` to mask its NVIC interrupt and publish pending work;
foreground `poll()` services at most two event causes per call. Both layers
depend on the byte-stream port without importing wire-protocol definitions.
`dso_stm32_usb.hpp` owns the USB VID/PID and stock bench-trial defaults and
range check; `dso_usb_board.hpp` validates only board-profile eligibility.
The foreground service combines the transport and protocol contracts and
forward-declares the controller in its interface; implementation units include
the controller contract where required. `dso_usb_service.cpp` owns session
lifecycle, timeouts and transmit/receive service. `dso_usb_requests.cpp`
validates request frames, applies replay rules, dispatches commands and encodes
responses. It explicitly maps controller settings results to wire statuses;
application result enum values therefore do not depend on protocol numbering.
Both use the single `UsbProtocolService` state owner.

Keep interrupts limited to their peripheral owner and publish work for foreground service. Do not move UI, protocol or storage workflows into interrupt handlers. See the contracts in `dso_acquisition.hpp`, `dso_ui_controller.hpp` and `dso_usb_service.hpp`.

## Acquisition and presentation

`dso_stm32_acquisition.cpp` configures ADC1, TIM2 and DMA1 Channel 1 for waveform sampling. Capture planning and trigger behavior live in `dso_capture.cpp` and `dso_acquisition.cpp`; the DMA ring and record ownership are handled by `dso_dma_ring.cpp`. `FrameMetadata` is the per-record value snapshot declared in `dso_frame_metadata.hpp`, separate from the DMA acquisition owner. Projection and Spectrum forward-declare it in their interfaces and include its definition only where fields are read. `MeasurementEngine` includes the metadata and calibration value headers directly, storing its snapshots by value without pulling in either the DMA owner or projection API. `dso_scaling.cpp` owns waveform projection and timebase planning; calibration validation, conversion, fitting and bank operations live in `dso_calibration.cpp`. Completed records are published as immutable captures for UI processing. Held captures preserve their data while settings or later acquisitions change.

`dso_ui_controller.cpp` connects front-panel state, trigger/capture state, measurements and rendering. The current timebase catalogue has 18 triggered settings from 10 µs/div through 5 s/div; it does not include the former slow rolling and filtered acquisition path. LCD updates use the ILI9341 display path; `dso_stm32_lcd_dma.cpp` owns the bounded DMA transport used by the display renderer.

## Peripheral ownership

| Resource | Role |
| --- | --- |
| ADC1, TIM2, DMA1 Channel 1 | Waveform sample acquisition |
| ADC2, PA1–PA3 | Front-panel analog switches and controls |
| TIM1, DMA1 Channels 2, 3 and 5 | LCD write transport |
| TIM3, PA7 | Optional test output |
| TIM4, PB8/PB9 | Analog power/reference control |
| PA15 (JTDI), D3 | Active-low accepted-trigger activity LED |
| SWD | Retained for programming and debugging |

`dso_stm32_shared_guard.cpp` and the relevant peripheral adapters enforce resource preconditions and ownership boundaries. Optional test output and storage are separate profile choices, not required for the user interface.

The trigger activity LED is initialized after the board passes its UID guard. The platform releases JTAG while retaining SWD, then preloads the inactive HIGH level and configures PA15 as a 2 MHz push-pull output. D3 is wired through R28 (470 Ω) to +3.3 V, so driving PA15 LOW lights it. An accepted trigger restarts a nonblocking 100 ms foreground pulse. Acquisition owns one pending-event flag that survives rearm until the application consumes it. Multiple unseen edges coalesce into one indication. The pulse is independent of display refresh. See [Trigger LED behavior](trigger-led.md).

The LED indicates accepted software trigger events, not an electrical trigger strobe or timing measurement. Accepted edges can be indicated in eligible Auto, Normal, and Single captures. Auto free-run timeouts, held-waveform redraws, and crossings rejected before sufficient pre-trigger history do not create an LED pulse. The pulse remains active through Single's automatic HOLD and is forced inactive on startup/application faults; the HardFault/NMI entry path also drives PA15 inactive. Closely spaced accepted edges can make the LED appear continuously lit. The operator confirmed D3 working on the reference instrument; pulse timing has not been independently measured.

## Memory and storage

The current storage linker layout provides 63 KiB for the application and reserves one 1 KiB Flash page (0x0800FC00–0x0800FFFF). The target has 20 KiB SRAM; the linker reserves a 2 KiB stack region, which is not a measurement of stack high-water. The storage layer and board-specific Flash adapter are in `dso_storage.cpp` and `dso_stm32_storage.cpp`.

The storage service retains only current settings and nine-slot DC calibration; waveform persistence is removed. The page is not erased automatically at boot. Explicit Save Config or Save Cal operations erase/program the page; reset or power failure during that operation can lose the prior saved data. CRC, readback and final commit validation reject incomplete records. The prior layout is not migrated, and its old data is no longer preserved. The page has a 32-byte envelope, a 36-byte settings record and up to 204 bytes of calibration data (272 bytes total), leaving 752 bytes unused. `dso_storage.cpp` names and statically checks the schema-2 envelope offsets. The settings/calibration codec owns payload meaning, and saved timebase validation depends directly on the shared catalogue contract rather than waveform scaling.

Legacy storage schemas are not supported or migrated. Current records retain CRC validation. Acquisition exports raw ADC samples; obsolete filtering metadata and its presentation paths have been removed.

## Status and limits

Firmware readiness and capture publication status describe software state. They do not prove continuous external sample integrity or characterize analog accuracy. The configured sample rate is not a bandwidth specification. The default nominal gain and zero values are board-specific starting parameters, not a calibration that transfers to another instrument.

The ordinary profile keeps USB off and ships with zero UID, board identity and VID/PID values. The opt-in `DSO_USB_STOCK_BENCH_TRIAL` profile uses the factory-standard fixed pull-up. The production wiring described in the [USB hardware guide](usb-hardware.md) uses a switched 3.0–3.6 V pull-up and VBUS sensing.

Studio 2 adds settings readback, revision-checked remote writes and selected-mode captures to the existing waveform protocol. The controller processes physical controls before USB work and keeps each held record immutable during export. See [Studio 2 design](studio-2-design.md), [USB protocol](usb-protocol.md), [verification](../host/windows/VERIFICATION.md) and [release readiness](release-readiness.md) for measured resource and bench results.

Configure the MCU UID and corresponding nonzero identity in `dso_board_bringup.hpp` for your instrument. Follow [Getting started](getting-started.md) and [Compatibility](compatibility.md) before programming.

## Windows Studio

Studio is a Qt application organized into five compiled libraries, one
header-only shared-contract target, and one executable. The target graph
separates protocol encoding from session orchestration, with transport and
presentation in their own modules. `dso_shared_contracts` exports only the
shared include root: capture and protocol expose it because their public headers
include shared contracts; session and UI consume it privately where their
implementations use the contracts directly.

| Target | Owns | Direct dependency boundary |
| --- | --- | --- |
| `dso_shared_contracts` | Shared compile-time instrument and wire contracts | Header-only include interface |
| `dso_capture` | Capture model, demo signal, file I/O, measurements and spectrum analysis | `dso_shared_contracts`, Qt Core |
| `dso_protocol` | USB wire codec, validation and shared-endian adapter | `dso_shared_contracts`, Qt Core |
| `dso_transport` | Byte transport interface and its Qt metaobject | Qt Core |
| `dso_serial_transport` | Qt SerialPort adapter and endpoint discovery | `dso_transport`, Qt SerialPort |
| `dso_session` | Event-driven instrument session and default serial constructor | `dso_capture`, `dso_protocol`, `dso_transport`, Qt Core; shared contracts and serial transport privately |
| `dso_ui` | Main window and instrument/presentation widgets | `dso_session`, Qt Widgets; shared contracts and serial transport privately |
| `DSO138Studio` | Application entry point | `dso_ui` |

### Shared instrument values

`dso_instrument_codes.hpp` owns stable byte codes for trigger modes, edges,
input coupling, calibration kind, UI state and the capture edge-triggered flag;
Studio's `instrument_code` names are aliases to those shared values, with one
range-check helper used by protocol decoding, capture validation and settings
setters to keep signed inputs and byte fields on the same code bounds.
`instrument_limits.hpp` preserves its ADC resolution, maximum-code, timebase
count and 1,024-sample capacity names as aliases to the shared contracts for
source-compatible consumers. Internal capture validation, measurements,
spectrum analysis, trigger controls and session sample checks include only the
ADC, capture, trigger-position or timebase contract they use. `protocol.hpp`
retains its existing capability names as aliases to the shared protocol
contract. These direct dependencies keep shared bounds single-sourced without
making each implementation depend on an aggregate limits header.
`instrument_labels.hpp` supplies trigger-mode names to session status and UI
formatting without introducing a session-to-UI dependency. Protocol and capture
structures retain their primitive field types, so USB and saved-data layouts do
not depend on UI vocabulary.

### `dso_capture`

`capture_model.hpp` defines and validates raw records and calibrated conversion
in `capture.cpp`. It owns `UnavailableHostIntervalMs`, the sentinel for a missing
or saturated host-side record interval. Optional error-output parameters
forward-declare `QString`, so model consumers do not import Qt string
implementation headers unless needed.
`demo_capture.hpp/.cpp` owns the built-in synthetic trace. `capture_io.hpp/.cpp`
owns JSON and CSV persistence, with bounds in `capture_internal.hpp`.
`measurements.hpp/.cpp` and `spectrum.hpp/.cpp` implement signal analysis.

`capture.hpp` remains the source-compatible umbrella for the model, measurements,
demo record and file operations; internal consumers use narrower interfaces.
Measurement declarations forward-declare `Capture`, while implementation files
include its model where the complete type is needed. Persistence, demo generation
and analysis depend on the capture model without making the model depend on them.

### `dso_protocol`

`protocol.hpp/.cpp` define and encode the wire protocol. ADC resolution and
trigger bounds come directly from the shared ADC and settings contracts rather
than the host-wide limits aggregate. The codec uses the shared `dso_crc32.hpp`
algorithm used by firmware while keeping its existing QByteArray-facing
`crc32()` interface. `wire_endian.hpp` keeps the Qt integer and `char*` boundary
while delegating byte conversion to shared
`dso_binary_endian.hpp`. The codec has no dependency on session state or capture
ownership; its `dso_protocol` target makes that boundary explicit. It also
encodes revisioned settings-write, `READ_SAMPLES` and single-`uint32_t` request
payloads behind a private request-payload interface, using the shared request
offsets where fields are structured. `Session` retains capture-range policy,
input validation, revision snapshots and write lifecycle. Callers validate
buffer bounds before forming pointers.

### `dso_session`

`byte_transport.hpp` defines stream operations, open-state inspection and
readiness/loss events; `dso_transport` owns its Qt metaobject independently of
the session and serial adapter. The injected-transport `Session` owns connection
lifecycle state through that interface. Explicit and
fault-driven shutdown share one endpoint-close helper, which clears the open
state before invoking the adapter because `close()` may synchronously emit a
loss event. Each caller retains its existing timer, state-reset and signal
ordering. The constructor tolerates a missing transport;
`connectPort()` reports the configuration error and leaves the session
disconnected instead of dereferencing a null adapter. Session calls transport
methods synchronously on its Qt thread, so an injected adapter must share that
thread for its owned lifetime; `unique_ptr` ownership does not move a QObject
between threads. Injected adapters must also be parentless QObjects, avoiding
overlapping Qt parent ownership and `unique_ptr` destruction.
`Session` itself remains on its construction thread because its value-member
timers do not migrate with the QObject.

`session_wire.cpp` maps decoded protocol metadata into capture metadata through
`toCaptureMetadata()` and provides Qt-facing little-endian reply-field readers.
Outgoing request payloads are encoded by the protocol module. `session.cpp`
owns connection and timer lifecycle;
`session_reply.cpp`, `session_settings.cpp`, `session_capture.cpp` and
`session_frame_transfer.cpp` separate reply validation, settings, capture/live
workflows and leased sample transfer while sharing one `Session` state owner.
`CaptureRunState` groups operation timeout, repeat progress, live mode and
acquisition timing; protocol reset clears the operation state together.
The repeated-series total remains latched through the final synchronous
`frameReady` callback so the UI can label that record after `busy()` becomes
false; `busy()` remains the activity query.
`LiveStartRetryState` groups the bounded Busy-retry flag, settings-read count
and revision snapshot. `session_timing.hpp` names the initial Busy delay, the
100 ms readiness recheck interval, and the seven-read limit so the retry bound
and pacing are visible in one policy header.

`InstrumentState` groups the device settings snapshot and readback availability.
`supportsExtendedMetadata()` derives frame-metadata capability from that
snapshot; `acceptInstrumentSettings()` commits a decoded or acknowledged value.
`canWriteSettings()` centralizes connection, state, HOLD and writable-capability
checks for timebase, trigger mode and trigger fields. Local Run/Pause uses its
separate capability flag. `FrameTransferState` groups the active lease token,
decoded metadata, bounded in-progress capture and sample offset; protocol reset
clears the transfer state with the session. `session_timing.hpp` defines the
exchange timer's 10 ms check cadence separately from its 250 ms retry threshold,
the retransmission limit, other polling intervals, capture completion grace,
and the bounded 15 ms BEGIN_FRAME readiness retry. The timer checks monotonic
elapsed time, so its cadence is not itself the retry delay. A
`DeferredAction` variant stores at most one
bounded continuation while a settings read is in flight. `handleSettings()`
detaches it before emitting Qt signals and dispatches it only when the session
generation, idle state and connection still match; this preserves the guard
against synchronous signal handlers changing session state. `SettingsWrite`
stores the pre-encoded full request and retains its revision separately for the
stale-readback guard. The public session interface uses Qt Core and the
transport abstraction, not QSerialPort types.

### `dso_serial_transport`

`serial_transport.hpp/.cpp` owns the Qt SerialPort adapter and endpoint discovery
through `availableSerialPorts()`. It implements `ByteTransport`; `MainWindow`
creates it at the application composition point and transfers ownership to
`Session`. `session_serial_adapter.cpp` implements the legacy default-serial
`Session` constructor in the session target. Qt's generated `Session` metatype
references that constructor even when a client injects another transport, so
`dso_session` carries a private serial-adapter link dependency. The session
logic still accepts another transport at runtime; the CMake dependency ensures
that linking the session target alone resolves its public constructor.

### `dso_ui` and application entry

`window_ui.cpp` keeps `buildUi()` as an orchestration method and separates style,
menus, acquisition controls, analysis/sidebar panels and status-bar setup into
private builders. `buildAnalysisPanels()` coordinates the plot views and sidebar;
dedicated builders construct its instrument, capture and measurement cards.
`trigger_controls.cpp` owns trigger detail inputs; combo item data carries
protocol edge codes so visible ordering remains independent of wire values.
`window.cpp` handles session lifecycle, serial-port choices through the
transport descriptor API, settings readback and control synchronization without
depending directly on Qt SerialPort.
During close, `MainWindow::closeEvent()` requests cancellation and allows a
bounded 1.8-second wait before resetting the connection if the operation has
not completed.

`InstrumentSettingsState` keeps the latest device snapshot separate from the
catalogue signature used to decide whether combo boxes need rebuilding.
`LiveDisplayState` groups the received-frame count and previous acquisition
epoch used by live-view admission and status reporting. `window_records.cpp`
handles capture admission, history and record presentation. `AnalysisLabels`
groups the non-owning label handles for calibration, signal measurements,
measurement quality and cursor readout; `window_ui.cpp` creates them and
`window_records.cpp` refreshes them for the selected capture. `window_format.cpp`
centralizes timebase, rate, voltage, measurement, settings, trigger and coupling
text; `window_format.hpp` forward-declares measurement-quality and settings
types, leaving full model dependencies in the implementation.
`window_capture_files.cpp` handles capture file actions and the demo record.
`main.cpp` starts the application.

The UI depends directly on the session, capture model and protocol settings it
presents. The session depends on its byte transport, protocol codec and capture
model. Measurement and spectrum analysis consume immutable raw samples and
capture metadata, including calibration; their headers forward-declare the
capture model when only a reference is needed. Protocol types remain independent
of the desktop capture model. The `.dso138` JSON and CSV formats are saved-data
interfaces, while USB is defined separately in [USB protocol](usb-protocol.md).
Keep these boundaries stable when reorganizing implementation files.
