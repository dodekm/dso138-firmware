# MATLAB USB client

`DsoUsbClient.m` reads captures from a USB CDC COM port using the DSO waveform protocol v1. The verified bench use is `getHeldFrame()` on a standalone held capture. The scope must be powered from its normal 9 V input; the USB connection carries data and joins the instrument ground to the PC ground.

The ordinary firmware profile keeps USB disabled. This client requires an explicitly configured `DSO_USB_STOCK_BENCH_TRIAL` image on the reference board. That trial uses the stock fixed pull-up; it is an experiment, not an electrically compliant production configuration. The firmware source defaults its VID/PID to zero. Do not redistribute a private bench image built with test identifiers.

## Read the held frame

In MATLAB R2025b, use the COM port shown by Windows Device Manager. The verified reference setup appeared as `COM7`; replace it with the port assigned on your PC.

```matlab
client = DsoUsbClient("COM7");
cleanup = onCleanup(@() delete(client));
frame = client.getHeldFrame();
plot(frame.TimeSeconds, double(frame.Raw));
xlabel("Time from trigger (s)");
ylabel("ADC count");
grid on;
```

`getHeldFrame()` sends INFO and read-only frame lease/sample commands, then releases the lease. It does not change settings or write Flash. `frame.Raw` contains 12-bit ADC codes; `frame.TimeSeconds` is calculated from the capture timer metadata. `frame.VoltageVolts` is empty when voltage conversion is unavailable. Otherwise the client converts with the metadata supplied by firmware. The verified smoke capture reported nominal calibration kind 1, not a measured calibration profile.

The returned metadata includes sample count, pretrigger count, timer frequency and divider, coupling, trigger mode/edge, and calibration fields. Raw codes are the authoritative data. A held export requires a complete frame to be available on the scope.

## Capture methods

The client also exposes `capture(timeoutMs)` and `captureRepeated(count, timeoutMs, gapSeconds)`. They request separate Single captures; they do not promise continuous or gap-free acquisition. Before requesting capture, the scope must be in HOLD and foreground work must be idle. A request while RUN is active may be rejected as busy; the host does not take over the scope or switch it to HOLD. The R4 `captureRepeated(3,uint32(3000),0)` test completed three valid 1,024-sample frames with Single mode and unique increasing epochs 20–22 in 2.944 seconds. The R5 repeat check also passed three 1,024-sample captures with valid 12-bit values, Single mode and unique epochs 793–795 in 2.524 seconds. The client waited for capture readiness before downloading each frame. These are separate captures with acquisition/presentation dead time, even when the requested extra gap is zero.

`delete(client)` closes the COM connection and releases any outstanding frame lease or host capture job. The `onCleanup` pattern above also performs cleanup if the script exits with an error. USB CDC ignores the nominal 115200 baud setting and the client does not toggle DTR.

## Current hardware check

On the reference board, Windows assigned COM7 to the CDC device without a driver error. INFO, fragmented responses, identical-request replay, conflicting request IDs and CRC resynchronization passed. Two 1,024-sample RAW downloads of the held frame produced identical hashes; a four-second slow-host frame lease passed. MATLAB R2025b completed INFO, validated frame timing/calibration metadata and the time axis, and plotted raw/converted values. The frame interval was 2 microseconds (`72 MHz / 144`) and calibration kind 1 was nominal, not measured.

A R5 host harness completed three 1,024-sample Single captures with jobs 1–3 and epochs 786–788. Acknowledgments took 0.47–1.05 ms. Completion took 160.23 ms for the first request, including a Busy retry while waiting for readiness, then 80.62 and 81.91 ms. RAW downloads took 47.57–53.06 ms (38.60–43.05 kB/s of sample bytes). Cancellation returned `Cancelled`; a 1 ms timeout returned `Timeout`, left the job `Cancelled`, and created no orphan job. These are separate captures; they do not establish fast or gap-free acquisition. An earlier 48.1 ms held-frame read (~42.6 kB/s) preceded the R4 break-only fix; all transfer rates are observations, not throughput guarantees.
